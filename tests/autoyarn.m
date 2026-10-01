// Deterministic tests: synthetic icons exercise the real decoder, colour
// selection, async cache, charts and renderer without any running apps.
#define KNIT_AUTO_TEST 1
#include "../src/autoyarn.m"
#include <stdatomic.h>
#include <stdio.h>
#include <ImageIO/ImageIO.h>

static NSImage* fixture;
static atomic_int samples;
static int ready_count;
static dispatch_semaphore_t gate;

static atomic_int sampled_by[256];          // samples per owner, pids 2000-2255
static atomic_bool slow_reads;              // pressure test: 1 ms per icon read
NSImage* knit_test_icon(pid_t pid) {
  assert(!pthread_main_np());
  atomic_fetch_add(&samples, 1);
  if (pid >= 2000 && pid < 2256) atomic_fetch_add(&sampled_by[pid - 2000], 1);
  if (atomic_load(&slow_reads)) usleep(1000);
  if (gate) dispatch_semaphore_wait(gate, DISPATCH_TIME_FOREVER);
  if (pid == 999) return nil;
  return fixture;
}
// In the pressure test a callback redraws that owner's window, exactly as
// main.c does, so any feedback between redraws and requests is real.
static NSMutableDictionary<NSNumber*, NSString*>* open_owners;
static void redraw_owner(pid_t pid) {
  NSString* name = open_owners[@(pid)];
  if (!name) return;                                   // closed: nothing to draw
  uint32_t yarn = pid % 2 ? 0xfff6f0de : 0xff203040; int chart = knit_chart_index("zigzag");
  knit_zigzag_yarn(name.UTF8String, pid, &yarn, &chart);
}
void knit_auto_yarn_ready(pid_t pid) {
  assert(pthread_main_np());
  assert(pid > 0);
  ready_count++;
  if (open_owners) redraw_owner(pid);
}

// A user's own chart, written where knit_charts_load finds it.
static void write_chart(const char* dir, const char* name, const uint32_t* pixels, int w, int h) {
  char path[1024]; snprintf(path, sizeof path, "%s/%s.png", dir, name);
  CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context=CGBitmapContextCreate((void*)pixels,w,h,8,w*4,space,
      kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Host);
  CGImageRef image=CGBitmapContextCreateImage(context);
  CGImageDestinationRef out=CGImageDestinationCreateWithURL(
      (__bridge CFURLRef)[NSURL fileURLWithPath:@(path)], CFSTR("public.png"), 1, NULL);
  CGImageDestinationAddImage(out,image,NULL); assert(CGImageDestinationFinalize(out));
  CFRelease(out); CGImageRelease(image); CGContextRelease(context); CGColorSpaceRelease(space);
}

static NSImage* make_icon(uint32_t first, uint32_t second) {
  uint32_t pixels[48*48];
  for (int i=0;i<48*48;i++) pixels[i] = i%48 < 36 ? first : second;
  CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context=CGBitmapContextCreate(pixels,48,48,8,48*4,space,
      kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(space);
  CGImageRef image=CGBitmapContextCreateImage(context);
  NSImage* result=[[NSImage alloc] initWithCGImage:image size:NSMakeSize(48,48)];
  CGImageRelease(image); CGContextRelease(context);
  return result;
}

static void drain(void) {
  dispatch_sync(g_auto_queue, ^{});
  CFAbsoluteTime deadline=CFAbsoluteTimeGetCurrent()+3;
  for (;;) {
    bool pending=false;
    for(int i=0;i<g_auto_count;i++) pending |= g_auto[i].pending;
    if (!pending) break;
    assert(CFAbsoluteTimeGetCurrent()<deadline);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.005,false);
  }
}

static int request(const char* name, pid_t pid, uint32_t* yarn) {
  int chart=-2;
  if (!knit_auto_yarn(name,pid,yarn,&chart)) {
    drain();
    assert(knit_auto_yarn(name,pid,yarn,&chart));
  }
  return chart;
}

static void check_chart(int index, uint32_t contrast) {
  assert(index>=0 && index<g_chart_count);
  struct knit_chart* c=&g_charts[index];
  assert(c->generated);
  bool base=false, motif=false;
  for(int i=0;i<c->w*c->h;i++) {
    assert(c->px[i]==0 || c->px[i]==contrast);
    base |= c->px[i]==0; motif |= c->px[i]==contrast;
  }
  assert(base && motif);
}

int main(void) { @autoreleasepool {
  knit_charts_load(NULL); // no personal configuration or custom files
  int builtins=g_chart_count;
  uint32_t base=0,contrast=0;
  assert(!icon_pixels(nil,&base,&contrast));
  assert(!icon_pixels(make_icon(0xff888888,0xffffffff),&base,&contrast));
  assert(!icon_pixels(make_icon(0x00000000,0x00000000),&base,&contrast));
  // A black-and-white logo whose black is faintly blue (ChatGPT's knot) has no
  // colour: dividing by brightness must not make a tinted near-black "navy".
  assert(!icon_pixels(make_icon(0xff28283c,0xfff4f4f6),&base,&contrast));
  assert(!icon_pixels(make_icon(0xff3a3a2a,0xffffffff),&base,&contrast));
  // One column of orange on grey (~2% of the icon, Calculator-like) still counts;
  // below that share, a stray highlight does not.
  { uint32_t accent[48*48];
    for(int i=0;i<48*48;i++) accent[i] = i%48==40 ? 0xffff9500 : 0xff505050;
    CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx=CGBitmapContextCreate(accent,48,48,8,48*4,space,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Host);
    CGImageRef im=CGBitmapContextCreateImage(ctx);
    assert(icon_pixels([[NSImage alloc] initWithCGImage:im size:NSMakeSize(48,48)],&base,&contrast));
    double ah,as,al; rgb2hsl(base,&ah,&as,&al); assert(ah>0.05 && ah<0.14);   // orange
    for(int i=0;i<48*48;i++) accent[i] = (i%48==40 && i/48<24) ? 0xffff9500 : 0xff505050;
    CGImageRelease(im); im=CGBitmapContextCreateImage(ctx);
    assert(!icon_pixels([[NSImage alloc] initWithCGImage:im size:NSMakeSize(48,48)],&base,&contrast));
    CGImageRelease(im); CGContextRelease(ctx); CGColorSpaceRelease(space); }
  // The Zigzag contrast rule at its boundary: lightness 0.62.
  assert(knit_zigzag_contrast(0xff9d9d9d)==KNIT_CREAM);          // 0.616: cream
  assert(knit_zigzag_contrast(0xff9f9f9f)==0xff5c5c5c);          // 0.624: 0.26 deeper
  assert(knit_zigzag_contrast(0xff000000)==KNIT_CREAM && knit_zigzag_contrast(0xffffffff)==0xffbcbcbc);
  assert(!icon_pixels(make_icon(0xcc990000,0xcc990000),&base,&contrast));
  assert(icon_pixels(make_icon(0xffff0000,0xffff0000),&base,&contrast));
  assert(contrast==0xfff6f0de);
  uint32_t opaque_base=base;
  assert(icon_pixels(make_icon(0xeeee0000,0xeeee0000),&base,&contrast));
  // Unpremultiplication restores the same histogram bin for these samples.
  assert(base==opaque_base);
  assert(icon_pixels(make_icon(0xffaaffaa,0xffaaffaa),&base,&contrast));
  double h,s,l,h2,s2,l2;
  rgb2hsl(base,&h,&s,&l); rgb2hsl(contrast,&h2,&s2,&l2);
  assert(l>l2 && l-l2>=0.20);
  // A diverse grid catches lightness compression and dark/light clamp edges.
  for(int r=17;r<256;r+=34) for(int g=17;g<256;g+=34) for(int b=17;b<256;b+=34) {
    uint32_t color=0xff000000u|(r<<16)|(g<<8)|b;
    if (!icon_pixels(make_icon(color,0xff113377),&base,&contrast)) continue;
    rgb2hsl(base,&h,&s,&l); rgb2hsl(contrast,&h2,&s2,&l2);
    assert(fabs(l-l2)>=0.195); // byte quantization allows half a percent
    assert(s<=0.61 && l>=0.29 && l<=0.73);
  }
  puts("PASS: real icon decoding, alpha/neutral filters, softened palettes and visible final contrast");

  fixture=make_icon(0xff3377dd,0xffffffff);
  uint32_t yarn=0x12345678; int chart=123;
  const char* invalid[]={NULL,"","Finder","Claude","Notion"};
  for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++)
    assert(!knit_auto_yarn(invalid[i],42,&yarn,&chart));
  assert(!knit_auto_yarn("Example",0,&yarn,&chart));
  assert(!knit_auto_yarn("Example",42,NULL,&chart));
  assert(!knit_auto_yarn("Example",42,&yarn,NULL));
  char long_name[128]; memset(long_name,'a',127);long_name[127]=0;
  assert(!knit_auto_yarn(long_name,42,&yarn,&chart));
  assert(yarn==0x12345678 && chart==123 && atomic_load(&samples)==0);
  g_app_rule_count=1;
  snprintf(g_app_rules[0].match,sizeof g_app_rules[0].match,"Personal");
  assert(!knit_auto_yarn("Personal App",42,&yarn,&chart));
  g_app_rule_count=0;

  // A deliberately blocked decoder must not stall a draw or duplicate work.
  gate=dispatch_semaphore_create(0);
  CFAbsoluteTime began=CFAbsoluteTimeGetCurrent();
  assert(!knit_auto_yarn("Example",42,&yarn,&chart));
  assert(!knit_auto_yarn("Example",42,&yarn,&chart));
  assert(CFAbsoluteTimeGetCurrent()-began<0.1);
  assert(yarn==0x12345678 && chart==123);
  dispatch_semaphore_signal(gate); drain(); gate=nil;
  assert(atomic_load(&samples)==1 && ready_count==1);
  chart=request("Example",42,&yarn);
  check_chart(chart,g_auto[0].contrast);
  uint32_t first=yarn; int first_chart=chart;
  for(int i=0;i<10000;i++) {
    assert(knit_auto_yarn("Example",42,&yarn,&chart));
    assert(yarn==first && chart==first_chart);
  }
  assert(atomic_load(&samples)==1);
  puts("PASS: authored/user rules, invalid inputs, nonblocking sampling, deduplication and cache hits");

  g_knit_pattern_by_app=false;
  assert(knit_auto_yarn("Example",42,&yarn,&chart) && chart==-1 && yarn==first);
  g_knit_pattern_by_app=true;
  assert(knit_auto_yarn("Example",42,&yarn,&chart) && chart==first_chart);
  g_knit_pattern_by_app=false;
  int count=g_chart_count;
  assert(request("Global first",43,&yarn)==-1 && g_chart_count==count);
  g_knit_pattern_by_app=true;
  assert(request("Global first",43,&yarn)>=0);
  // Reload, then generate in reverse order so stale indexes cannot pass.
  knit_charts_load(NULL); knit_flush_cache();
  assert(request("Global first",43,&yarn)==builtins);
  assert(request("Example",42,&yarn)==builtins+1 && yarn==first);
  check_chart(builtins+1,g_auto[0].contrast);
  assert(atomic_load(&samples)==2);
  puts("PASS: global/plain/By App transitions in both directions and reload with reordered chart indexes");

  // Missing icons can recover; neutral icons are sampled once per process.
  assert(!knit_auto_yarn("Launching",999,&yarn,&chart)); drain();
  int calls=atomic_load(&samples);
  assert(!knit_auto_yarn("Launching",999,&yarn,&chart));
  assert(atomic_load(&samples)==calls);
  g_auto[2].retry_after=0;
  assert(!knit_auto_yarn("Launching",999,&yarn,&chart));drain();
  assert(atomic_load(&samples)==calls+1);
  fixture=make_icon(0xff999999,0xffffffff);
  assert(!knit_auto_yarn("Grey",50,&yarn,&chart));drain();
  calls=atomic_load(&samples);
  assert(!knit_auto_yarn("Grey",50,&yarn,&chart));
  assert(atomic_load(&samples)==calls);
  fixture=make_icon(0xffdd7733,0xffffffff);
  assert(request("Grey",51,&yarn)>=0); // relaunched process has a fresh cache
  puts("PASS: missing-icon retry, negative caching and resampling after app relaunch");

  for(int i=0;i<160;i++) {
    char name[64];snprintf(name,sizeof name,"Unsupported %d",i);
    assert(request(name,1000+i,&yarn)>=0);
  }
  assert(g_auto_count==KNIT_AUTO_MAX);
  assert(g_chart_count<=builtins+KNIT_AUTO_MAX);
  // No retained chart accidentally contains a literal source-pattern colour.
  for(int i=0;i<g_auto_count;i++) if(g_auto[i].ok)
    check_chart(g_auto[i].chart,g_auto[i].contrast);
  puts("PASS: 160 apps cycle through bounded cache without exhausting the generated chart table");

  // Mode and chart generation can change while a decode is still in flight.
  gate=dispatch_semaphore_create(0);
  assert(!knit_auto_yarn("In flight",700,&yarn,&chart));
  knit_charts_load(NULL);knit_flush_cache();
  g_knit_pattern_by_app=false;
  dispatch_semaphore_signal(gate);drain();gate=nil;
  assert(request("In flight",700,&yarn)==-1);
  g_knit_pattern_by_app=true;
  assert(request("In flight",700,&yarn)>=0);
  // An authored override added during decoding still takes precedence.
  assert(!knit_auto_yarn("New personal",701,&yarn,&chart));
  g_app_rule_count=1;
  snprintf(g_app_rules[0].match,sizeof g_app_rules[0].match,"New personal");
  drain();
  assert(!knit_auto_yarn("New personal",701,&yarn,&chart));
  g_app_rule_count=0;
  assert(request("New personal",701,&yarn)>=0);
  puts("PASS: mode/reload/rule changes while icon work is in flight");

  // Full custom-chart table gracefully keeps the colour; reload restores motif.
  knit_charts_load(NULL);knit_flush_cache();
  g_chart_count=KNIT_CHART_MAX;
  assert(request("Table full",800,&yarn)==-1);
  knit_charts_load(NULL);knit_flush_cache();
  assert(request("Table full",800,&yarn)>=0);
  puts("PASS: chart-capacity fallback and recovery after reload");

  // Shared Zigzag: every app in its icon's colour, the 37 included, on a
  // cream zigzag, or a deeper shade of its own colour when that is pale.
  knit_charts_load(NULL);knit_flush_cache();
  int zig=knit_chart_index("zigzag");
  assert(zig>=0 && g_charts[zig].px[0]==CREAM);             // the shared zigzag is cream
  g_knit_pattern_by_app=true;  assert(!knit_zigzag_active());
  assert(knit_pattern_select("picnic") && !knit_zigzag_active());
  assert(knit_pattern_select("zigzag") && knit_zigzag_active());

  // A hand-designed app takes its icon's colour, not its authored one.
  assert(knit_app_rule("Claude"));
  fixture=make_icon(0xff1060d0,0xff1060d0);
  uint32_t zy=knit_app_rule("Claude")->color; int zc=zig;
  knit_zigzag_yarn("Claude",900,&zy,&zc);                    // first call only starts sampling
  assert(zy==knit_app_rule("Claude")->color && zc==zig);
  drain();
  assert(knit_zigzag_yarn("Claude",900,&zy,&zc));
  assert(zy!=knit_app_rule("Claude")->color && zc==zig);     // dark yarn: shared cream zigzag
  double zh,zs,zl; rgb2hsl(zy,&zh,&zs,&zl);
  assert(zl<0.62 && zh>0.5 && zh<0.75);                          // a softened blue

  // A pale yarn gets its own zigzag in a deeper shade of that yarn.
  fixture=make_icon(0xfff8eaa0,0xfff8eaa0);
  uint32_t pale=0xff000000; int pc=zig;
  knit_zigzag_yarn("Pale",901,&pale,&pc); drain();
  pale=0xff000000; pc=zig;
  assert(knit_zigzag_yarn("Pale",901,&pale,&pc));
  rgb2hsl(pale,&zh,&zs,&zl); assert(zl>=0.62);
  assert(pc!=zig); check_chart(pc,zigzag_yarn(pale));
  assert(zigzag_yarn(pale)!=CREAM);
  int pale_chart=pc, charts_now=g_chart_count;
  pc=zig; knit_zigzag_yarn("Pale",901,&pale,&pc);            // a cache hit: nothing rebuilt
  assert(pc==pale_chart && g_chart_count==charts_now);

  // No usable icon colour: the usual colour stays, and still gets a visible zigzag.
  fixture=make_icon(0xff888888,0xffffffff);
  uint32_t notion=knit_app_rule("Notion")->color; int nc=zig;
  knit_zigzag_yarn("Notion",902,&notion,&nc); drain();
  notion=knit_app_rule("Notion")->color; nc=zig;
  knit_zigzag_yarn("Notion",902,&notion,&nc);
  assert(notion==knit_app_rule("Notion")->color);
  assert(nc!=zig); check_chart(nc,zigzag_yarn(notion));

  // Switching modes reuses the app's one chart slot instead of adding charts.
  fixture=make_icon(0xfff8eaa0,0xff304080);
  uint32_t my=0; int mc=zig;
  knit_zigzag_yarn("Switcher",903,&my,&mc); drain();
  my=0; mc=zig; knit_zigzag_yarn("Switcher",903,&my,&mc);
  int zig_slot=mc; charts_now=g_chart_count;
  assert(zig_slot!=zig);
  g_knit_pattern_by_app=true;
  uint32_t by_yarn; int by_chart=request("Switcher",903,&by_yarn);
  assert(by_chart==zig_slot && g_chart_count==charts_now);   // rebuilt in place as its By App motif
  for(int i=0;i<g_auto_count;i++) if(g_auto[i].pid==903) check_chart(by_chart,g_auto[i].contrast);
  assert(knit_pattern_select("zigzag"));
  my=0; mc=zig; knit_zigzag_yarn("Switcher",903,&my,&mc);
  assert(mc==zig_slot && g_chart_count==charts_now);
  check_chart(mc,zigzag_yarn(my));

  // A pattern reload renumbers charts; the zigzag is rebuilt, never stale.
  knit_charts_load(NULL);knit_flush_cache();
  assert(knit_pattern_select("zigzag"));
  my=0; mc=knit_chart_index("zigzag"); knit_zigzag_yarn("Switcher",903,&my,&mc);
  assert(mc>=0 && mc!=knit_chart_index("zigzag")); check_chart(mc,zigzag_yarn(my));
  puts("PASS: shared Zigzag: icon colours for every app, cream or deeper-shade zigzag, fallbacks, one chart per app across modes and reloads");

  // A colour the user wrote in apps.conf wins over the icon, in Zigzag too;
  // only the built-in collection gives way. Pale personal colours still get
  // a visible zigzag.
  fixture=make_icon(0xff1060d0,0xff1060d0);
  g_app_rule_count=2;
  snprintf(g_app_rules[0].match,sizeof g_app_rules[0].match,"Greenapp"); g_app_rules[0].color=0xff00ff00; g_app_rules[0].chart[0]=0;
  snprintf(g_app_rules[1].match,sizeof g_app_rules[1].match,"Creamapp"); g_app_rules[1].color=0xfff6f0de; g_app_rules[1].chart[0]=0;
  assert(knit_app_rule_personal(knit_app_rule("Greenapp")) && knit_app_rule_personal(knit_app_rule("Creamapp")));
  assert(!knit_app_rule_personal(knit_app_rule("Claude")) && !knit_app_rule_personal(NULL));
  assert(!knit_app_rule_personal(&g_app_rules[2]));             // beyond the live rules
  zig=knit_chart_index("zigzag");
  uint32_t green=0xff00ff00; int gc=zig;
  knit_zigzag_yarn("Greenapp",910,&green,&gc); drain();
  green=0xff00ff00; gc=zig; knit_zigzag_yarn("Greenapp",910,&green,&gc);
  assert(green==0xff00ff00 && gc==zig);                        // not the icon's blue
  uint32_t cream=0xfff6f0de; int cc=zig;
  assert(knit_zigzag_yarn("Creamapp",911,&cream,&cc));
  assert(cream==0xfff6f0de && cc!=zig && cc>=0); check_chart(cc,zigzag_yarn(cream));
  drain();                  // its chart slot started an icon read; finish it before gating
  // A personal rule added while the icon is still being read also wins.
  gate=dispatch_semaphore_create(0);
  uint32_t late=0xff808080; int lc=zig;
  knit_zigzag_yarn("Lateapp",912,&late,&lc);
  g_app_rule_count=3;
  snprintf(g_app_rules[2].match,sizeof g_app_rules[2].match,"Lateapp"); g_app_rules[2].color=0xff203040; g_app_rules[2].chart[0]=0;
  dispatch_semaphore_signal(gate); drain(); gate=nil;
  late=0xff203040; lc=zig; knit_zigzag_yarn("Lateapp",912,&late,&lc);
  assert(late==0xff203040 && lc==zig);
  g_app_rule_count=0;
  puts("PASS: shared Zigzag keeps personal apps.conf colours, including rules added mid-sampling");

  // A zigzag the user drew themselves is shown exactly as drawn: red and
  // translucent stitches kept, on dark and pale apps alike, across reloads.
  char dir[]="/tmp/knit-zigzag-XXXXXX"; assert(mkdtemp(dir));
  uint32_t mine[12*6];
  for(int i=0;i<12*6;i++) mine[i]= i%4==0 ? 0xffd02020 : i%4==1 ? 0x80400000 : 0;
  write_chart(dir,"zigzag",mine,12,6);
  for(int round=0;round<2;round++) {
    knit_charts_load(dir); knit_flush_cache();
    assert(knit_pattern_select("zigzag") && knit_zigzag_active());
    zig=knit_chart_index("zigzag");
    assert(g_charts[zig].custom && !g_charts[knit_chart_index("picnic")].custom);
    uint32_t* before=malloc(sizeof(uint32_t)*g_charts[zig].w*g_charts[zig].h);
    memcpy(before,g_charts[zig].px,sizeof(uint32_t)*g_charts[zig].w*g_charts[zig].h);
    fixture=make_icon(0xfff8eaa0,0xfff8eaa0);                  // pale
    uint32_t py=0; int pc2=zig; knit_zigzag_yarn("Pale custom",920+round,&py,&pc2); drain();
    py=0; pc2=zig; knit_zigzag_yarn("Pale custom",920+round,&py,&pc2);
    assert(pc2==zig && py!=0);                                  // icon colour, the user's chart
    fixture=make_icon(0xff1060d0,0xff1060d0);                  // dark
    uint32_t dy=0; int dc=zig; knit_zigzag_yarn("Dark custom",930+round,&dy,&dc); drain();
    dy=0; dc=zig; knit_zigzag_yarn("Dark custom",930+round,&dy,&dc);
    assert(dc==zig);
    assert(!memcmp(before,g_charts[zig].px,sizeof(uint32_t)*g_charts[zig].w*g_charts[zig].h));
    free(before);
  }
  char png[1100]; snprintf(png,sizeof png,"%s/zigzag.png",dir); unlink(png); rmdir(dir);
  knit_charts_load(NULL); knit_flush_cache();
  assert(!g_charts[knit_chart_index("zigzag")].custom);
  puts("PASS: a custom zigzag.png is shown as drawn for dark and pale apps, before and after reload");

  // No room for a pale app's own zigzag: plain knitting, never cream on cream.
  assert(knit_pattern_select("zigzag"));
  zig=knit_chart_index("zigzag");
  fixture=make_icon(0xfff8eaa0,0xfff8eaa0);
  int saved_count=g_chart_count;
  g_chart_count=KNIT_CHART_MAX;
  uint32_t fy=0; int fc=zig; knit_zigzag_yarn("Crowded",940,&fy,&fc); drain();
  fy=0; fc=zig; assert(knit_zigzag_yarn("Crowded",940,&fy,&fc));
  assert(fc==-1);
  g_chart_count=saved_count;
  knit_charts_load(NULL); knit_flush_cache(); assert(knit_pattern_select("zigzag"));
  zig=knit_chart_index("zigzag");
  fy=0; fc=zig; knit_zigzag_yarn("Crowded",940,&fy,&fc);
  assert(fc>=0 && fc!=zig);                                     // recovers once there is room
  // Overlong names and invalid owners cannot be sampled: readable anyway.
  char longname[200]; memset(longname,'x',199); longname[199]=0;
  uint32_t ly=0xfff6f0de; int lc2=zig; knit_zigzag_yarn(longname,941,&ly,&lc2); assert(lc2==-1);
  uint32_t iy=0xfff6f0de; int ic=zig; knit_zigzag_yarn("No owner",0,&iy,&ic); assert(ic==-1);
  uint32_t dark=0xff203040; int dk=zig; knit_zigzag_yarn("No owner",0,&dark,&dk); assert(dk==zig);

  puts("PASS: pale Zigzag stays readable with a full chart table, long names and no owner");

  // Pressure: 200 owners for 64 slots (136 turned away at once, more than
  // the slots), 1 ms icon reads, and callbacks that really redraw. Turned-away owners must each be asked again, and the whole
  // system must then go quiet by itself: no loop of evictions and redraws.
  // Owner 2066 closes while waiting; owner 999 has no icon at all.
  drain();
  open_owners = [NSMutableDictionary dictionary];
  for (int i = 0; i < 200; i++) open_owners[@(2000 + i)] = [NSString stringWithFormat:@"Pressure %d", i];
  open_owners[@999] = @"Iconless";
  memset(sampled_by, 0, sizeof sampled_by);
  atomic_store(&slow_reads, true);
  int samples_before = atomic_load(&samples);
  for (int i = 0; i < 200; i++) redraw_owner(2000 + i);          // one draw pass, in order
  redraw_owner(999);
  // The first 64 fill every slot; 2064-2199 and 999 are all turned away.
  assert(g_deferred_count == 137);
  [open_owners removeObjectForKey:@2066];                       // closes while waiting
  CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 10, quiet_since = 0;
  int last = -1;
  for (;;) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
    bool busy = g_deferred_count > 0;
    for (int i = 0; i < g_auto_count; i++) busy |= g_auto[i].pending;
    int now = atomic_load(&samples);
    if (busy || now != last) { quiet_since = CFAbsoluteTimeGetCurrent(); last = now; }
    else if (CFAbsoluteTimeGetCurrent() - quiet_since > 0.3) break;
    assert(CFAbsoluteTimeGetCurrent() < deadline);             // a loop never goes quiet
  }
  int total = atomic_load(&samples) - samples_before;
  assert(total <= 2 * 201);                                     // bounded, not a loop
  for (int i = 0; i < 200; i++) {                               // every open owner was asked
    int n = atomic_load(&sampled_by[i]);
    if (i == 66 ? n != 0 : n < 1) { fprintf(stderr, "owner %d sampled %d times\n", 2000 + i, n); abort(); }
  }
  int settled_samples = atomic_load(&samples), settled_ready = ready_count;
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);        // and it stays quiet
  assert(atomic_load(&samples) == settled_samples && ready_count == settled_ready);
  atomic_store(&slow_reads, false);
  open_owners = nil;
  printf("PASS: 200 owners for 64 slots settle by themselves (%d icon reads), every turned-away owner retried, closed ones skipped\n", total);
  g_knit_pattern_by_app=true;
  return 0;
}}
