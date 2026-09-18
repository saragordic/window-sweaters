#include <ApplicationServices/ApplicationServices.h>
#include <CoreText/CoreText.h>
#include <mach/mach_time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "misc/knit.h"
#include "misc/chart.h"
#include "misc/apps.h"
#include "catalogue.h"

static CGContextRef canvas(int w, int h, int scale) {
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef c = CGBitmapContextCreate(NULL, w*scale, h*scale, 8, 0, cs,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(cs);
  assert(c);
  CGContextScaleCTM(c, scale, scale);
  CGContextTranslateCTM(c, 0, h); CGContextScaleCTM(c, 1, -1);
  return c;
}

static void text_at(CGContextRef c, float x, float y, const char* text, float size) {
  CFStringRef s = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
  CTFontRef font = CTFontCreateWithName(CFSTR("HelveticaNeue"), size, NULL);
  CGColorRef ink = CGColorCreateGenericRGB(.19, .20, .18, 1);
  const void* keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
  const void* vals[] = { font, ink };
  CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, vals, 2,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef a = CFAttributedStringCreate(NULL, s, attrs);
  CTLineRef line = CTLineCreateWithAttributedString(a);
  CGContextSaveGState(c);
  CGContextTranslateCTM(c, x, y); CGContextScaleCTM(c, 1, -1);
  CGContextSetTextPosition(c, 0, 0); CTLineDraw(line, c);
  CGContextRestoreGState(c);
  CFRelease(line); CFRelease(a); CFRelease(attrs); CGColorRelease(ink);
  CFRelease(font); CFRelease(s);
}

static void write_image(CGContextRef c, const char* path) {
  CGImageRef img=CGBitmapContextCreateImage(c);
  CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)path,strlen(path),false);
  CGImageDestinationRef dest=CGImageDestinationCreateWithURL(url,CFSTR("public.png"),1,NULL);
  assert(dest); CGImageDestinationAddImage(dest,img,NULL); assert(CGImageDestinationFinalize(dest));
  CFRelease(dest); CFRelease(url); CGImageRelease(img);
}

static void preview(const char* path) {
  const char* apps[] = { "Finder", "Microsoft Teams", "Claude", "Codex",
                       "Spotify", "Notion", "WhatsApp", "Figma", "Google Chrome", "Paper" };
  const char* yarns[] = { "Two-tone blue · navy", "Violet · lavender · porcelain",
    "Apricot · vanilla · rose", "Blue · cream · pink", "Leaf green · butter yellow",
    "Paper white · ink black", "Meadow · pistachio · pink", "Mauve · pastel colour blocks",
    "Blue · red · yellow · green", "Paper blue · white · denim" };
  const int W=1240, H=1300;
  CGContextRef c=canvas(W,H,2);
  CGContextSetRGBFillColor(c,.972,.957,.930,1);
  CGContextFillRect(c,CGRectMake(0,0,W,H));
  text_at(c,44,57,"The sweater drawer",31);
  text_at(c,44,85,"Ten little colour stories. Original repeats, knitted by the app.",15);
  for(int i=0;i<10;i++) {
    float x=44+(i%3)*398, y=128+(i/3)*280;
    const struct app_rule* r=knit_app_rule(apps[i]); assert(r);
    text_at(c,x,y+14,apps[i],19);
    text_at(c,x,y+37,yarns[i],12);
    CGRect win=CGRectMake(x+14,y+66,340,124);
    knit_draw(c,win,10,14,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,.996,.990,.978,1);
    CGContextAddPath(c,p); CGContextFillPath(c); CFRelease(p);
    for(int dot=0;dot<3;dot++) {
      CGContextSetRGBFillColor(c,.83,.81,.78,1);
      CGContextFillEllipseInRect(c,CGRectMake(win.origin.x+12+dot*12,win.origin.y+12,6,6));
    }
    CGContextSetRGBFillColor(c,.92,.90,.86,1);
    CGContextFillRect(c,CGRectMake(win.origin.x,win.origin.y+30,win.size.width,.5));
    // A magnified strip below each real-size window shows the yarn repeat.
    CGContextSaveGState(c);
    CGContextClipToRect(c,CGRectMake(x, y+224,368,26));
    knit_draw(c,CGRectMake(x-50,y+250,470,100),10,26,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  text_at(c,44,1271,"14 pt window borders · 26 pt yarn details · actual renderer at 2×",12);
  write_image(c,path); CGContextRelease(c);
}

static void refined_preview(const char* path) {
  const char* apps[] = {"Notion", "Google Chrome", "Paper", "Finder"};
  const char* notes[] = {"White knit / small charcoal dots / white corners", "Butter stripes / red accents / yellow corners",
                         "Paper blue / layered white sheets", "Two-tone blue checks / navy stitches"};
  CGContextRef c=canvas(1120,770,2);
  CGContextSetRGBFillColor(c,.977,.969,.953,1);CGContextFillRect(c,CGRectMake(0,0,1120,770));
  text_at(c,40,50,"Four familiar faces, freshly knitted",29);
  text_at(c,40,80,"10 pt borders, with enlarged stitch details below. Rendered by the app.",14);
  for(int i=0;i<4;i++) {
    float x=40+(i%2)*550,y=116+(i/2)*310;
    const struct app_rule* r=knit_app_rule(apps[i]);assert(r);
    text_at(c,x,y+18,apps[i],20);text_at(c,x,y+43,notes[i],13);
    CGRect win=CGRectMake(x+12,y+72,470,118);
    knit_draw(c,win,10,10,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,1,1,1,1);CGContextAddPath(c,p);CGContextFillPath(c);CFRelease(p);
    for(int d=0;d<3;d++) {CGContextSetRGBFillColor(c,.85,.84,.81,1);CGContextFillEllipseInRect(c,CGRectMake(x+25+d*12,y+84,6,6));}
    CGContextSaveGState(c);CGContextClipToRect(c,CGRectMake(x,y+225,494,34));
    knit_draw(c,CGRectMake(x-50,y+259,610,100),10,34,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  write_image(c,path);CGContextRelease(c);
}

static void fabric_preview(const char* path) {
  const char* apps[] = {"WhatsApp", "Codex", "Messages", "Spotify"};
  const char* notes[] = {"Broad stripes / solid green corner sections", "Fine cream and pink chevrons / complete repeat",
                         "Filled polka dots / a fine green fabric", "The familiar checks / a softer fabric"};
  CGContextRef c=canvas(1120,770,2);
  CGContextSetRGBFillColor(c,.977,.969,.953,1);CGContextFillRect(c,CGRectMake(0,0,1120,770));
  text_at(c,40,50,"Soft fabric, tidy corners",29);
  text_at(c,40,80,"Same 10 pt width. Quieter texture, with enlarged fabric details below.",14);
  for(int i=0;i<4;i++) {
    float x=40+(i%2)*550,y=116+(i/2)*310;
    const struct app_rule* r=knit_app_rule(apps[i]);assert(r);
    text_at(c,x,y+18,apps[i],20);text_at(c,x,y+43,notes[i],13);
    CGRect win=CGRectMake(x+12,y+72,470,118);
    knit_draw(c,win,10,10,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,1,1,1,1);CGContextAddPath(c,p);CGContextFillPath(c);CFRelease(p);
    for(int d=0;d<3;d++) {CGContextSetRGBFillColor(c,.85,.84,.81,1);CGContextFillEllipseInRect(c,CGRectMake(x+25+d*12,y+84,6,6));}
    CGContextSaveGState(c);CGContextClipToRect(c,CGRectMake(x,y+225,494,34));
    knit_draw(c,CGRectMake(x-50,y+259,610,100),10,34,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  write_image(c,path);CGContextRelease(c);
}

static void yarn_preview(const char* path) {
  const char* apps[] = {"Google Chrome", "Notion", "WhatsApp", "ChatGPT"};
  const char* notes[] = {"The same colours / more visible knitted stitches", "White wool / small, spaced charcoal dots",
                         "The same stripes / soft, visible yarn", "Your reference / unchanged fine chevrons"};
  CGContextRef c=canvas(1120,770,2);
  CGContextSetRGBFillColor(c,.977,.969,.953,1);CGContextFillRect(c,CGRectMake(0,0,1120,770));
  text_at(c,40,50,"A little more sweater",29);
  text_at(c,40,80,"Same colours, patterns, corners and 10 pt width. More readable yarn for the first three.",14);
  for(int i=0;i<4;i++) {
    float x=40+(i%2)*550,y=116+(i/2)*310;
    const struct app_rule* r=knit_app_rule(apps[i]);assert(r);
    text_at(c,x,y+18,apps[i],20);text_at(c,x,y+43,notes[i],13);
    CGRect win=CGRectMake(x+12,y+72,470,118);
    knit_draw(c,win,10,10,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,1,1,1,1);CGContextAddPath(c,p);CGContextFillPath(c);CFRelease(p);
    for(int d=0;d<3;d++) {CGContextSetRGBFillColor(c,.85,.84,.81,1);CGContextFillEllipseInRect(c,CGRectMake(x+25+d*12,y+84,6,6));}
    CGContextSaveGState(c);CGContextClipToRect(c,CGRectMake(x,y+225,494,34));
    knit_draw(c,CGRectMake(x-50,y+259,610,100),10,34,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  write_image(c,path);CGContextRelease(c);
}

static void corner_preview(const char* path) {
  const char* apps[] = {"Google Chrome", "Notion", "Granola", "Figma"};
  const char* notes[] = {"Butter stripes / little red accents / yellow corners", "White wool / small charcoal dots / white corners",
                         "Deep grey and green stripes / grey corners", "Pastel blocks continue through the corners"};
  CGContextRef c=canvas(1120,770,2);
  CGContextSetRGBFillColor(c,.977,.969,.953,1);CGContextFillRect(c,CGRectMake(0,0,1120,770));
  text_at(c,40,50,"A little finishing touch",29);
  text_at(c,40,80,"Same 10 pt width. Simple patterns, finished with solid corner patches.",14);
  for(int i=0;i<4;i++) {
    float x=40+(i%2)*550,y=116+(i/2)*310;
    const struct app_rule* r=knit_app_rule(apps[i]);assert(r);
    text_at(c,x,y+18,apps[i],20);text_at(c,x,y+43,notes[i],13);
    CGRect win=CGRectMake(x+12,y+72,470,118);
    knit_draw(c,win,10,10,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,1,1,1,1);CGContextAddPath(c,p);CGContextFillPath(c);CFRelease(p);
    for(int d=0;d<3;d++) {CGContextSetRGBFillColor(c,.85,.84,.81,1);CGContextFillEllipseInRect(c,CGRectMake(x+25+d*12,y+84,6,6));}
    CGContextSaveGState(c);CGContextClipToRect(c,CGRectMake(x,y+225,494,34));
    knit_draw(c,CGRectMake(x-50,y+259,610,100),10,34,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  write_image(c,path);CGContextRelease(c);
}

static void finish_preview(const char* path) {
  const char* apps[] = {"Notion", "Claude", "Paper", "Safari"};
  const char* notes[] = {"Mostly white wool / small, spaced charcoal dots", "Apricot corners / stars stay on the straight edges",
                         "Little sheets continue around the corners / no patches", "Compass motifs continue through the corners"};
  CGContextRef c=canvas(1120,770,2);
  CGContextSetRGBFillColor(c,.977,.969,.953,1);CGContextFillRect(c,CGRectMake(0,0,1120,770));
  text_at(c,40,50,"Sweaters with a considered finish",29);
  text_at(c,40,80,"Paper without patches. Equal corner patches where they suit the sweater.",14);
  for(int i=0;i<4;i++) {
    float x=40+(i%2)*550,y=116+(i/2)*310;
    const struct app_rule* r=knit_app_rule(apps[i]);assert(r);
    text_at(c,x,y+18,apps[i],20);text_at(c,x,y+43,notes[i],13);
    CGRect win=CGRectMake(x+12,y+72,470,118);
    knit_draw(c,win,10,10,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,1,1,1,1);CGContextAddPath(c,p);CGContextFillPath(c);CFRelease(p);
    for(int d=0;d<3;d++) {CGContextSetRGBFillColor(c,.85,.84,.81,1);CGContextFillEllipseInRect(c,CGRectMake(x+25+d*12,y+84,6,6));}
    CGContextSaveGState(c);CGContextClipToRect(c,CGRectMake(x,y+225,494,34));
    knit_draw(c,CGRectMake(x-50,y+259,610,100),10,34,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  write_image(c,path);CGContextRelease(c);
}

// Render the actual built-ins, without loading personal profiles or PNG overrides.
static void favourites_preview(const char* path) {
  const char* apps[] = {"Spotify", "ChatGPT", "Google Chrome", "Paper"};
  const char* notes[] = {"Original raised stitches / green and butter checks", "Cream and pink chevrons / patterned corners",
                         "Butter stripes, sage blocks and little red details", "Your Paper sweater / unchanged"};
  CGContextRef c=canvas(1120,770,2);
  CGContextSetRGBFillColor(c,.977,.969,.953,1);CGContextFillRect(c,CGRectMake(0,0,1120,770));
  text_at(c,40,50,"A little character, brought back",29);
  text_at(c,40,80,"10 pt borders with enlarged yarn details. ChatGPT and Codex share the same sweater.",14);
  for(int i=0;i<4;i++) {
    float x=40+(i%2)*550,y=116+(i/2)*310;
    const struct app_rule* r=knit_app_rule(apps[i]);assert(r);
    text_at(c,x,y+18,apps[i],20);text_at(c,x,y+43,notes[i],13);
    CGRect win=CGRectMake(x+12,y+72,470,118);
    knit_draw(c,win,10,10,r->color,knit_chart_index(r->chart),0,1);
    CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
    CGContextSetRGBFillColor(c,1,1,1,1);CGContextAddPath(c,p);CGContextFillPath(c);CFRelease(p);
    for(int d=0;d<3;d++) {CGContextSetRGBFillColor(c,.85,.84,.81,1);CGContextFillEllipseInRect(c,CGRectMake(x+25+d*12,y+84,6,6));}
    CGContextSaveGState(c);CGContextClipToRect(c,CGRectMake(x,y+225,494,34));
    knit_draw(c,CGRectMake(x-50,y+259,610,100),10,34,r->color,knit_chart_index(r->chart),0,1);
    CGContextRestoreGState(c);
  }
  write_image(c,path);CGContextRelease(c);
}

// Render the actual built-ins, without loading personal profiles or PNG overrides.
// A separate review plate for every app: actual-size window, yarn detail,
// and all four corners enlarged from the same 10-point geometry.
static void individual_review(const char* directory) {
  knit_charts_load(NULL);
  for (int i = 0; i < CATALOGUE_COUNT; i++) {
    const struct catalogue_entry* entry = &catalogue[i];
    const struct app_rule* rule = knit_app_rule(entry->match); assert(rule);
    int chart = knit_chart_index(rule->chart); assert(chart >= 0);
    CGContextRef c = canvas(720, 530, 2);
    CGContextSetRGBFillColor(c, .977, .969, .953, 1);
    CGContextFillRect(c, CGRectMake(0, 0, 720, 530));
    text_at(c, 38, 47, entry->name, 29);
    text_at(c, 38, 74, entry->pattern, 14);
    CGRect win = CGRectMake(48, 112, 620, 140);
    knit_draw(c, win, 10, 10, rule->color, chart, 0, 1);
    CGPathRef path = CGPathCreateWithRoundedRect(win, 10, 10, NULL);
    CGContextSetRGBFillColor(c, 1, 1, 1, 1);
    CGContextAddPath(c, path); CGContextFillPath(c);
    text_at(c, 68, 154, "10-point window border", 13);
    text_at(c, 38, 286, "10-point stitch geometry / enlarged 4x", 12);
    CGContextSaveGState(c);
    CGContextClipToRect(c, CGRectMake(38, 301, 640, 40));
    CGContextTranslateCTM(c, 38 - 4*100, 301 - 4*102);
    CGContextScaleCTM(c, 4, 4);
    knit_draw(c, win, 10, 10, rule->color, chart, 0, 1);
    CGContextRestoreGState(c);
    text_at(c, 38, 375, "All four corners / enlarged from the same window", 12);
    const float sx[] = {38, 638, 638, 38}, sy[] = {102, 102, 222, 222};
    const char* labels[] = {"Top left", "Top right", "Bottom right", "Bottom left"};
    for (int corner = 0; corner < 4; corner++) {
      float x = 58 + corner * 166, y = 390;
      CGContextSaveGState(c);
      CGContextClipToRect(c, CGRectMake(x, y, 80, 80));
      CGContextTranslateCTM(c, x - 2*sx[corner], y - 2*sy[corner]);
      CGContextScaleCTM(c, 2, 2);
      knit_draw(c, win, 10, 10, rule->color, chart, 0, 1);
      CGContextSetRGBFillColor(c, 1, 1, 1, 1);
      CGContextAddPath(c, path); CGContextFillPath(c);
      CGContextRestoreGState(c);
      text_at(c, x, 496, labels[corner], 12);
    }
    CFRelease(path);
    char output[1024];
    snprintf(output, sizeof output, "%s/app-%02d.png", directory, i + 1);
    write_image(c, output); CGContextRelease(c);
  }
}

static void catalogue_preview(const char* directory, bool revised) {
  const int selected[] = {1, 3, 8, 14, 20, 28, 34};
  const int starts[] = {0, 8, 15, 22, 29, CATALOGUE_COUNT};
  const char* titles[] = {"The collection / 01", "The collection / 02",
    "The collection / 03", "The collection / 04", "The collection / 05"};
  knit_charts_load(NULL);
  for (int page = 0; page < (revised ? 1 : 5); page++) {
    CGContextRef c = canvas(1120, 1370, 2);
    CGContextSetRGBFillColor(c, .977, .969, .953, 1);
    CGContextFillRect(c, CGRectMake(0, 0, 1120, 1370));
    text_at(c, 40, 51, revised ? "Back to the classics" : titles[page], 30);
    text_at(c, 40, 81, "Window Sweaters / App-inspired colours, a little whimsy, and real knitted stitches.", 14);
    int first = revised ? 0 : starts[page];
    int end = revised ? 7 : starts[page + 1];
    for (int slot = first; slot < end; slot++) {
      int i = revised ? selected[slot] : slot;
      const struct catalogue_entry* entry = &catalogue[i];
      const struct app_rule* rule = knit_app_rule(entry->match); assert(rule);
      int chart = knit_chart_index(rule->chart); assert(chart >= 0);
      int cell = slot - first;
      float x = 40 + (cell % 2) * 550, y = 115 + (cell / 2) * 299;
      text_at(c, x, y + 19, entry->name, 20);
      text_at(c, x, y + 43, entry->palette, 12);
      CGRect win = CGRectMake(x + 12, y + 70, 470, 112);
      knit_draw(c, win, 10, 10, rule->color, chart, 0, 1);
      CGPathRef path = CGPathCreateWithRoundedRect(win, 10, 10, NULL);
      CGContextSetRGBFillColor(c, 1, 1, 1, 1);
      CGContextAddPath(c, path); CGContextFillPath(c); CFRelease(path);
      for (int dot = 0; dot < 3; dot++) {
        CGContextSetRGBFillColor(c, .85, .84, .81, 1);
        CGContextFillEllipseInRect(c, CGRectMake(x + 25 + dot * 12, y + 82, 6, 6));
      }
      text_at(c, x + 27, y + 141, entry->pattern, 14);
      CGContextSaveGState(c);
      CGContextClipToRect(c, CGRectMake(x, y + 219, 494, 34));
      knit_draw(c, CGRectMake(x - 50, y + 253, 610, 100), 10, 34, rule->color, chart, 0, 1);
      CGContextRestoreGState(c);
    }
    text_at(c, 40, 1330, "10 pt window borders / enlarged yarn details / actual app renderer at 2x", 12);
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s-%d.png", directory, revised ? "classics" : "collection", page + 1);
    assert(length > 0 && length < (int)sizeof path);
    write_image(c, path); CGContextRelease(c);
  }
}

// docs/collection/styles-comparison.png: By App beside the shared Zigzag.
// Zigzag's main yarns come from app icons, which differ between Macs, so
// they are fixed here as sampled on 2026-09-18; rerun --styles after updating
// them. Contrast yarns use the app's own rule, knit_zigzag_contrast().
#ifndef STY_TITLE_Y
#define STY_TITLE_Y 52
#define STY_SUB_Y 82
#define STY_HEAD_Y 137
#define STY_HEAD_SIZE 25
#define STY_LABEL_Y 198
#define STY_LABEL_SIZE 20
#endif
static void styled_text(CGContextRef c, float x, float y, const char* text, float size) {
  CFStringRef s = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
  CTFontRef font = CTFontCreateWithName(CFSTR("HelveticaNeue"), size, NULL);
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const CGFloat rgba[] = { 63/255., 66/255., 60/255., 1 };
  CGColorRef ink = CGColorCreate(space, rgba);
  const void* keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
  const void* vals[] = { font, ink };
  CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, vals, 2,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef a = CFAttributedStringCreate(NULL, s, attrs);
  CTLineRef line = CTLineCreateWithAttributedString(a);
  CGContextSaveGState(c);
  CGContextTranslateCTM(c, x, y); CGContextScaleCTM(c, 1, -1);
  CGContextSetTextPosition(c, 0, 0); CTLineDraw(line, c);
  CGContextRestoreGState(c);
  CFRelease(line); CFRelease(a); CFRelease(attrs); CGColorRelease(ink);
  CGColorSpaceRelease(space); CFRelease(font); CFRelease(s);
}

static void style_card(CGContextRef c, float x, float y, uint32_t base, int chart) {
  CGRect win = CGRectMake(x + 12, y + 12, 470, 112);
  knit_flush_cache();
  knit_draw(c, win, 10, 12, base, chart, 0, 1);
  CGPathRef p = CGPathCreateWithRoundedRect(win, 10, 10, NULL);
  CGContextSetRGBFillColor(c, 1, 1, 1, 1); CGContextAddPath(c, p); CGContextFillPath(c); CFRelease(p);
  for (int d = 0; d < 3; d++) {
    CGContextSetRGBFillColor(c, .85, .84, .81, 1);
    CGContextFillEllipseInRect(c, CGRectMake(win.origin.x + 13 + d * 12, win.origin.y + 12, 6, 6));
  }
  CGContextSaveGState(c);
  CGContextClipToRect(c, CGRectMake(x, y + 159, 494, 36));
  knit_draw(c, CGRectMake(x - 50, y + 195, 610, 100), 10, 36, base, chart, 0, 1);
  CGContextRestoreGState(c);
}

static void styles_comparison(const char* path) {
  static const struct { const char* name; uint32_t icon; } apps[] = {
    { "Finder", 0xff4ba1c0 }, { "Microsoft Teams", 0xff585896 },
    { "Claude", 0xffb78070 }, { "Codex", 0xff0a84ff },       // Codex: no icon colour, keeps its own
    { "Spotify", 0xff3aa65e }, { "Notion", 0xfff5f5f2 },     // Notion: no icon colour, keeps its own
    { "WhatsApp", 0xff3aa667 }, { "Figma", 0xffc04b5b } };
  knit_charts_load(NULL);
  const int W = 2240, H = 1320;
  CGContextRef c = canvas(W, H, 2);
  CGContextSetRGBFillColor(c, .977, .969, .953, 1); CGContextFillRect(c, CGRectMake(0, 0, W, H));
  styled_text(c, 40, STY_TITLE_Y, "Window Sweaters", 32);
  styled_text(c, 40, STY_SUB_Y, "Your favourite apps, two ways to wear them.", 16);
  styled_text(c, 40, STY_HEAD_Y, "By App", STY_HEAD_SIZE);
  styled_text(c, 1160, STY_HEAD_Y, "Zigzag", STY_HEAD_SIZE);
  CGContextSetRGBFillColor(c, 217/255., 217/255., 207/255., 1);
  CGContextFillRect(c, CGRectMake(1119.5, 112, 1, 1122));
  int zigzag = knit_chart_index("zigzag"); assert(zigzag >= 0);
  const struct knit_chart shared = g_charts[zigzag];
  for (int i = 0; i < 8; i++) {
    const struct app_rule* rule = knit_app_rule(apps[i].name); assert(rule);
    float y = 221 + (i / 2) * 270;
    for (int half = 0; half < 2; half++) {
      float x = 40 + half * 1120 + (i % 2) * 540;
      styled_text(c, x, STY_LABEL_Y + (i / 2) * 270, apps[i].name, STY_LABEL_SIZE);
      if (!half) { style_card(c, x, y, rule->color, knit_chart_index(rule->chart)); continue; }
      // The shared zigzag, knitted in this app's contrast yarn.
      uint32_t contrast = knit_zigzag_contrast(apps[i].icon);
      uint32_t* px = malloc(sizeof(uint32_t) * shared.w * shared.h); assert(px);
      for (int k = 0; k < shared.w * shared.h; k++) px[k] = (shared.px[k] >> 24) >= 128 ? contrast : 0;
      int slot = g_chart_count++;
      g_charts[slot] = shared; g_charts[slot].px = px;
      style_card(c, x, y, apps[i].icon, slot);
      g_chart_count--;
      free(px);
    }
  }
  styled_text(c, 40, 1284, "12 pt borders / enlarged yarn details / rendered by Window Sweaters", 13);
  styled_text(c, 1160, 1284, "Zigzag uses each app\xe2\x80\x99s icon colour where it has one, with a cream or deeper zigzag for contrast.", 13);
  write_image(c, path); CGContextRelease(c);
}

static void patterns_preview(const char* path) {
  const char* names[]={"picnic","ribbon","posy","twinkle","candy-stripe","zigzag"};
  const char* titles[]={"Picnic Checks","Ribbon Stripes","Little Bows","Tiny Stars","Candy Stripes","Zigzag"};
  const uint32_t colors[]={0xff497641,0xff3d554b,0xff9283c1,0xffd58561,0xff89b8d2,0xffad7197};
  CGContextRef c=canvas(1050,680,2);
  CGContextSetRGBFillColor(c,.972,.957,.930,1); CGContextFillRect(c,CGRectMake(0,0,1050,680));
  text_at(c,42,55,"Pick a repeat",30);
  text_at(c,42,85,"Six original patterns. Each keeps the app’s main yarn colour.",15);
  for(int i=0;i<6;i++) {
    float x=42+(i%2)*500, y=125+(i/2)*165;
    text_at(c,x,y+22,titles[i],19);
    CGContextSaveGState(c); CGContextClipToRect(c,CGRectMake(x,y+44,452,52));
    knit_draw(c,CGRectMake(x-60,y+96,580,130),10,52,colors[i],knit_chart_index(names[i]),0,1);
    CGContextRestoreGState(c);
  }
  text_at(c,42,650,"Actual app renderer · enlarged to show the stitch construction",12);
  write_image(c,path); CGContextRelease(c);
}

static void chrome_preview(const char* path) {
  const int W = 920, H = 430;
  CGContextRef c = canvas(W, H, 2);
  CGContextSetRGBFillColor(c,.949,.937,.906,1);
  CGContextFillRect(c,CGRectMake(0,0,W,H));
  text_at(c,40,55,"A sweater for Chrome",29);
  text_at(c,40,84,"Ivory yarn with red, green, yellow and occasional blue checks.",14);
  const struct app_rule* r = knit_app_rule("Google Chrome"); assert(r);
  CGRect win=CGRectMake(56,132,560,220);
  knit_draw(c,win,10,14,r->color,knit_chart_index(r->chart),0,1);
  CGPathRef p=CGPathCreateWithRoundedRect(win,10,10,NULL);
  CGContextSetRGBFillColor(c,.987,.982,.964,1);
  CGContextAddPath(c,p); CGContextFillPath(c); CFRelease(p);
  for (int dot=0;dot<3;dot++) {
    CGContextSetRGBFillColor(c,.80,.79,.75,1);
    CGContextFillEllipseInRect(c,CGRectMake(72+dot*14,147,8,8));
  }
  text_at(c,72,210,"Google Chrome",19);
  text_at(c,72,238,"A little colour, knitted in.",14);
  CGRect detail=CGRectMake(700,160,120,145);
  knit_draw(c,detail,10,28,r->color,knit_chart_index(r->chart),0,1);
  text_at(c,680,363,"A closer look at the yarn",12);
  text_at(c,40,400,"Actual app rendering · Regular border on the left · Enlarged detail on the right",12);
  CGImageRef img=CGBitmapContextCreateImage(c);
  CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)path,strlen(path),false);
  CGImageDestinationRef dest=CGImageDestinationCreateWithURL(url,CFSTR("public.png"),1,NULL);
  assert(dest); CGImageDestinationAddImage(dest,img,NULL); assert(CGImageDestinationFinalize(dest));
  CFRelease(dest); CFRelease(url); CGImageRelease(img); CGContextRelease(c);
}

static uint32_t pixel(CGContextRef c,int x,int y) {
  return *(uint32_t*)((char*)CGBitmapContextGetData(c)+y*CGBitmapContextGetBytesPerRow(c)+x*4);
}

// Yarn highlights vary in brightness; hue must still match the corner yarn.
static bool same_yarn(uint32_t a, uint32_t b) {
  int ac[3] = {(a >> 16) & 255, (a >> 8) & 255, a & 255};
  int bc[3] = {(b >> 16) & 255, (b >> 8) & 255, b & 255};
  int am = 0, bm = 0;
  for (int i = 0; i < 3; i++) { if (ac[i] > am) am = ac[i]; if (bc[i] > bm) bm = bc[i]; }
  for (int i = 0; i < 3; i++)
    if (abs(ac[i] * bm - bc[i] * am) > 3 * (am + bm)) return false;
  return true;
}

// A cuff must follow the circular arc, without coloured islands inside it.
static void verify_curved_cuff(void) {
  uint32_t yarn = 0;
  int id = g_chart_count++;
  g_charts[id] = (struct knit_chart){.w=1,.h=1,.px=&yarn,
    .cuff_color=0xffff0000u,.sculpted_yarn=true,.defined_yarn=true};
  const float radii[] = {0,10,28};
  for(int scale=1;scale<=2;scale++) for(int k=0;k<3;k++) {
    CGRect win=CGRectMake(30,30,180,110), outer=CGRectInset(win,-10,-10);
    CGContextRef c=canvas(250,180,scale), mask=canvas(250,180,scale);
    knit_draw(c,win,radii[k],10,0xffeeeeeEu,id,0,1);
    CGMutablePathRef ring=CGPathCreateMutable();
    float r=radii[k]+10,w=10.f/3.f;
    CGPathAddRoundedRect(ring,NULL,outer,r,r);
    CGPathAddRoundedRect(ring,NULL,CGRectInset(outer,w,w),r-w,r-w);
    CGContextAddPath(mask,ring);CGContextSetRGBFillColor(mask,1,1,1,1);
    CGContextEOFillPath(mask);CFRelease(ring);
    for(int y=0;y<180*scale;y++) for(int x=0;x<250*scale;x++) {
      uint32_t p=pixel(c,x,y), m=pixel(mask,x,y)>>24;
      int red=(p>>16)&255,green=(p>>8)&255;
      if(m==255 && !(red>2*green)) { fprintf(stderr,"cuff mismatch scale=%d radius=%g x=%d y=%d pixel=%08x\n",scale,radii[k],x,y,p); abort(); }
      if(m==0 && (p>>24)==255) assert(abs(red-green)<3);
    }
    CGContextRelease(c);CGContextRelease(mask);
  }
  g_charts[id]=(struct knit_chart){0};g_chart_count--;
  puts("PASS: curved cuff hue/width, no inner colour islands at 1x/2x and three radii");
}

// Exact width and rounded-ring coverage, including all four internal mitres.
static void verify_corners(void) {
  const float radii[] = {0, 10, 28, 200};
  
  for (int scale = 1; scale <= 2; scale++)
    for (int style = 0; style < CATALOGUE_COUNT; style++)
      for (int n = 0; n < 4; n++) {
        CGRect win = CGRectMake(30, 30, 180, 110);
        float radius = fminf(radii[n], 55);
        CGContextRef actual = canvas(250, 180, scale), mask = canvas(250, 180, scale);
        knit_draw(actual, win, radii[n], 10, 0xff6ea77b, knit_chart_index(catalogue[style].chart), 0, 1);
        CGMutablePathRef ring = CGPathCreateMutable();
        CGPathAddRoundedRect(ring, NULL, CGRectInset(win, -10, -10), radius + 10, radius + 10);
        CGPathAddRoundedRect(ring, NULL, CGRectInset(win, 1, 1), fmaxf(0, radius - 1), fmaxf(0, radius - 1));
        CGContextSetRGBFillColor(mask, 1, 1, 1, 1);
        CGContextAddPath(mask, ring); CGContextEOFillPath(mask); CFRelease(ring);
        for (int y = 0; y < 180 * scale; y++)
          for (int x = 0; x < 250 * scale; x++) {
            unsigned expected = pixel(mask, x, y) >> 24, alpha = pixel(actual, x, y) >> 24;
            if (expected == 255 && alpha < 250) { fprintf(stderr, "corner gap: scale=%d style=%d radius=%g x=%d y=%d alpha=%u\n", scale, style, radius, x, y, alpha); abort(); } // no holes at mitres
            if (expected == 0) assert(alpha <= 5); // width/content remain unchanged
            // Every fully covered pixel within a solid curved patch must have
            // the same yarn, including near the inner arc (no pale square line).
            float dx = (x + 0.5f) / scale, dy = (y + 0.5f) / scale;
            bool in_corner = (dx < 30 + radius || dx > 210 - radius)
                             && (dy < 30 + radius || dy > 140 - radius);
            if (n == 1 && expected == 255 && in_corner &&
                g_charts[knit_chart_index(catalogue[style].chart)].solid_corners)
              if (!same_yarn(pixel(actual, x, y), pixel(actual, 25*scale, 31*scale))) {
                fprintf(stderr, "patch mismatch %s scale%d x%d y%d actual%08x reference%08x\n", catalogue[style].chart, scale, x, y, pixel(actual,x,y),pixel(actual,25*scale,31*scale)); abort();
              }
          }
        if (g_charts[knit_chart_index(catalogue[style].chart)].solid_corners && n == 1) {
          // These pixels sit on all four solid arc sections, away from antialiasing.
          const int points[][2] = {{25,31},{215,31},{25,139},{215,139}};
          for (int i = 0; i < 4; i++)
            assert(same_yarn(pixel(actual, points[i][0]*scale, points[i][1]*scale),
                           pixel(actual, points[0][0]*scale, points[0][1]*scale)));
          uint32_t corner = pixel(actual, 25*scale, 31*scale);
          assert((corner >> 24) == 255);
        }
        CGContextRelease(actual); CGContextRelease(mask);
      }
  puts("PASS: all 37 profiles, unchanged 10 pt width, all four opaque corners, square/rounded/clamped radii at 1x/2x");
}

// A contrasting test fabric exposes the exact patch boundary independently
// of an app motif's base-colour gaps. Every side must start/end at the tangent,
// including lengths that leave different remainders after whole tile repeats.
static void verify_equal_patches(void) {
  assert(g_chart_count < KNIT_CHART_MAX);
  int chart = g_chart_count++;
  uint32_t yarn[36];
  for (int i = 0; i < 36; i++) yarn[i] = 0xffffffffu;
  g_charts[chart] = (struct knit_chart){.name = "patch-boundary-test", .w = 6,
      .h = 6, .px = yarn, .solid_corners = true, .corner_color = 0xffff0000u};
  int saved_anchor = g_knit_anchor;
  const int widths[] = {180, 187, 299}, heights[] = {110, 137, 173};
  for (int scale = 1; scale <= 2; scale++)
    for (int anchor = KNIT_ANCHOR_CORNER; anchor <= KNIT_ANCHOR_CENTRE; anchor++)
      for (int size = 0; size < 3; size++) {
        g_knit_anchor = anchor;
        int w = widths[size], h = heights[size];
        CGContextRef c = canvas(w + 80, h + 80, scale);
        knit_draw(c, CGRectMake(40, 40, w, h), 10, 10, 0xffffffffu, chart, 0, 1);
        int min_patch = 255, max_patch = 0;
        for (int side = 0; side < 4; side++) {
          int length = (side < 2 ? w : h) + 20;
          for (int offset = 15 * scale; offset < (length - 15) * scale; offset++) {
            int x = side < 2 ? 30 * scale + offset : (side == 2 ? 35 : w + 45) * scale;
            int y = side >= 2 ? 30 * scale + offset : (side == 0 ? 35 : h + 45) * scale;
            uint32_t p = pixel(c, x, y);
            bool patch = ((p >> 16) & 255) > 2 * ((p >> 8) & 255);
            bool expected = offset < 20 * scale || offset >= (length - 20) * scale;
            assert((p >> 24) == 255);
            assert(patch == expected);
            if (expected) {
              int red = (p >> 16) & 255;
              if (red < min_patch) min_patch = red;
              if (red > max_patch) max_patch = red;
            }
          }
        }
        assert(max_patch - min_patch > 8); // single-colour patches still have yarn relief
        CGContextRelease(c);
      }
  g_knit_anchor = saved_anchor;
  knit_flush_cache();
  g_charts[chart] = (struct knit_chart){0};
  g_chart_count--;
  assert(!g_charts[knit_chart_index("atelier-paper")].solid_corners);
  puts("PASS: equal patch extents on every edge at varied sizes, both anchors and 1x/2x; Paper unpatched");
}

static void verify(void) {
  verify_corners();
  verify_equal_patches();
  verify_curved_cuff();
  int chart=knit_chart_index("atelier-paper");
  for(int s=1;s<=2;s++) {
    CGContextRef a=canvas(520,400,s), b=canvas(520,400,s);
    knit_draw(a,CGRectMake(40,40,300,200),10,14,0xff96b9dd,chart,0,1);
    knit_draw(b,CGRectMake(40,40,400,260),10,14,0xff96b9dd,chart,0,1);
    assert(pixel(a,150*s,120*s)==0); // content remains transparent
    assert(pixel(a,5*s,5*s)==0);     // outside the sweater stays transparent
    assert((pixel(a,120*s,32*s)>>24)==255); // wool stays opaque
    for(int y=27*s;y<39*s;y++) for(int x=80*s;x<250*s;x++)
      assert(pixel(a,x,y)==pixel(b,x,y)); // unpatched repeats retain their phase during resize
    CGContextRelease(a); CGContextRelease(b);
  }
  const char* materials[] = {"atelier-paper", "atelier-notion", "atelier-spotify"};
  for (int m = 0; m < 3; m++) {
    chart = knit_chart_index(materials[m]);
    // Colourwork must look identical after any previous plain-stitch choice.
    int saved_stitch=g_knit_stitch;
    CGContextRef reference=canvas(320,200,2);
    g_knit_stitch=KNIT_STOCKINETTE; knit_flush_cache();
    knit_draw(reference,CGRectMake(30,30,240,130),10,14,0xff87bddb,chart,0,1);
    for(int style=KNIT_RIB;style<KNIT_STITCH_COUNT;style++) {
      CGContextRef sample=canvas(320,200,2);
      g_knit_stitch=style; knit_flush_cache();
      knit_draw(sample,CGRectMake(30,30,240,130),10,14,0xff87bddb,chart,0,1);
      assert(CGBitmapContextGetBytesPerRow(reference)==CGBitmapContextGetBytesPerRow(sample));
      assert(memcmp(CGBitmapContextGetData(reference),CGBitmapContextGetData(sample),
                    CGBitmapContextGetBytesPerRow(reference)*CGBitmapContextGetHeight(reference))==0);
      CGContextRelease(sample);
    }
    CGContextRelease(reference); g_knit_stitch=saved_stitch; knit_flush_cache();
    // Exercise cache eviction and reuse across distinct per-window colourways.
    CGContextRef c=canvas(320,200,2);
    for(int i=0;i<80;i++)
      knit_draw(c,CGRectMake(30,30,240,130),10,14,0xff445566+i*4096,chart,0,1);
    CGContextRelease(c); knit_flush_cache();
  }
  puts("PASS: 1×/2× transparency, opaque wool, unpatched resize phase, colourwork independent of plain style, cache eviction");
}

static void benchmark(const char* app) {
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  const int sizes[][2]={{600,400},{1400,900},{2560,1440}};
  for(int i=0;i<3;i++) {
    CGContextRef c=canvas(sizes[i][0]+80,sizes[i][1]+80,2);
    CGRect win=CGRectMake(40,40,sizes[i][0],sizes[i][1]);
    knit_flush_cache();
    const struct app_rule* rule=knit_app_rule(app); assert(rule);
    int chart=knit_chart_index(rule->chart);
    uint64_t start=mach_absolute_time();
    knit_draw(c,win,10,14,0xff96b9dd,chart,0,1);
    double cold=(mach_absolute_time()-start)*(double)tb.numer/tb.denom/1e6;
    start=mach_absolute_time();
    for(int j=0;j<24;j++) knit_draw(c,win,10,14,0xff96b9dd,chart,0,1);
    double warm=(mach_absolute_time()-start)*(double)tb.numer/tb.denom/1e6/24;
    printf("%dx%d @2x: first %.2f ms; cached %.2f ms\n",sizes[i][0],sizes[i][1],cold,warm);
    CGContextRelease(c);
  }
}

int main(int argc,char** argv) {
  assert(knit_color_for_app("Example App") == knit_color_for_app("example app"));
  assert(knit_color_for_app(NULL) == knit_color_for_app(""));
  knit_charts_load("charts"); // no user preferences or files touched
  if(argc>1 && !strcmp(argv[1],"--bench")) benchmark(argc>2 ? argv[2] : "Paper");
  else if(argc>2 && !strcmp(argv[1],"--individual")) individual_review(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--catalogue")) catalogue_preview(argv[2], false);
  else if(argc>2 && !strcmp(argv[1],"--classics")) catalogue_preview(argv[2], true);
  else if(argc>2 && !strcmp(argv[1],"--favourites")) favourites_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--finish")) finish_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--corners")) corner_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--yarn")) yarn_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--fabric")) fabric_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--refined")) refined_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--chrome")) chrome_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--patterns")) patterns_preview(argv[2]);
  else if(argc>2 && !strcmp(argv[1],"--styles")) styles_comparison(argv[2]);
  else if(argc>1) preview(argv[1]);
  else verify();
  knit_flush_cache();
  return 0;
}
