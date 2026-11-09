// Reuse fake defaults/native-menu fixtures; no NSApplication or live windows.
#define main original_menu_main
#include "menu.m"
#undef main
#include "../src/parse.c"

// Unrelated parser dependencies; inactive/dim branches below are production code.
void knit_flush_cache(void) {}
bool knit_pattern_select(const char* name) { (void)name; return false; }

static void parse_arg(const char* value) {
  struct settings settings = {0};
  char* arg = (char*)value;
  assert(parse_settings(&settings, 1, &arg) == BORDER_UPDATE_MASK_ALL);
}
int main(int argc, char** argv) {
  @autoreleasepool {
    KnitTestDefaults* d = KnitTestDefaults.standardUserDefaults;
    parse_arg("dim=0.72");
    knit_load_prefs();
    assert(g_knit_dim == .72f && g_knit_grey == 0);
    knit_save_prefs();
    assert(!d.values[@"inactive"]);
    d.values[@"inactive"] = @"invalid";
    knit_load_prefs();
    assert(g_knit_dim == .72f && g_knit_grey == 0);
    const char* modes[] = {"inactive=colour", "inactive=dim", "inactive=grey"};
    for (int i=0; i<3; i++) {
      d.values[@"inactive"] = @(modes[i]);
      g_knit_dim = .72f; g_knit_grey = .4f;
      knit_load_prefs();
      assert(g_knit_dim == (i==1 ? KNIT_INACTIVE_DIM : 0));
      assert(g_knit_grey == (i==2 ? 1 : 0));
    }
    parse_arg("inactive=color"); assert(g_knit_dim==0 && g_knit_grey==0);
    parse_arg("inactive=gray"); assert(g_knit_dim==0 && g_knit_grey==1);
    parse_arg("gray=0.5"); assert(g_knit_grey==.5f);
    parse_arg("grey=0.25"); assert(g_knit_grey==.25f);
    parse_arg("inactive=dim"); assert(g_knit_dim==KNIT_INACTIVE_DIM && g_knit_grey==0);
    puts("PASS: real parser aliases/masks; missing/invalid preferences preserve dim; all three saved looks restore");
    // A settings file runs after the saved menu choices and wins, as it does
    // for width= and chart=. main.c restores prefs, then runs sweatersrc.
    d.values[@"inactive"] = @"inactive=colour";
    knit_load_prefs();
    parse_arg("dim=0.72");
    assert(fabsf(g_knit_dim - 0.72f) < 1e-6 && g_knit_grey == 0);
    puts("PASS: a settings file's dim= still wins over the saved menu look, like every other setting");
    // Compare another saved scalar with the same later-parser override.
    d.values[@"width"] = @18;
    d.values[@"gauge"] = @10;
    knit_load_prefs();
    assert(current_width == 18 && g_knit.rows == 10);
    struct settings settings = {.border_width = current_width};
    char* config[] = {"width=28", "gauge=14"};
    assert(parse_settings(&settings, 2, config) == BORDER_UPDATE_MASK_ALL);
    assert(settings.border_width == 28 && g_knit.rows == 14);
    // dim= overrides the dim component; it does not cancel a saved Grey look.
    d.values[@"inactive"] = @"inactive=grey";
    knit_load_prefs();
    parse_arg("dim=0.72");
    assert(g_knit_dim == .72f && g_knit_grey == 1);
    puts("PASS: later width/gauge settings win; dim and grey remain independent components");
  }
  return 0;
}
