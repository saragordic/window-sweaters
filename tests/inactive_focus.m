#define main original_app_filter_main
#include "app_filter.m"
#undef main

int main(void) {
  @autoreleasepool {
    table_init(&g_windows, 8, hash_wid, equal_wid);
    struct border a = {.target_wid=11}, b = {.target_wid=12};
    table_add(&g_windows, &a.target_wid, &a);
    table_add(&g_windows, &b.target_wid, &b);
    const char styles[] = {BORDER_STYLE_KNIT, BORDER_STYLE_ROUND,
                          BORDER_STYLE_ROUND_UNIFORM, BORDER_STYLE_SQUARE};
    for (int s=0;s<4;s++) for (int mode=0;mode<3;mode++) {
      g_settings.border_style=styles[s];
      g_knit_dim=mode==1 ? .35f : 0;
      g_knit_grey=mode==2 ? 1 : 0;
      a.focused=true; b.focused=false;
      a.needs_redraw=b.needs_redraw=false;
      a.metadata_dirty=b.metadata_dirty=false;
      assert(windows_window_focus(&g_windows,12));
      assert(!a.focused && b.focused && a.metadata_dirty && b.metadata_dirty);
      bool repaint=s!=0 || mode!=0;
      assert(a.needs_redraw==repaint && b.needs_redraw==repaint);
      a.needs_redraw=b.needs_redraw=false;
      a.metadata_dirty=b.metadata_dirty=false;
      assert(windows_window_focus(&g_windows,12));
      assert(!a.needs_redraw && !b.needs_redraw && !a.metadata_dirty && !b.metadata_dirty);
      assert(!windows_window_focus(&g_windows,999));
      assert(!a.focused && !b.focused && b.needs_redraw==repaint);
      assert(windows_window_focus(&g_windows,11));
      assert(a.focused && !b.focused && a.needs_redraw==repaint);
    }
    table_clear(&g_windows);
    puts("PASS: focus gain/loss, repeat focus, untracked front window, and all four border styles in all three modes");
  }
  return 0;
}
