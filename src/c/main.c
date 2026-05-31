#include <pebble.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define KEY_TEXT             0
#define KEY_FONT             1
#define TEXT_BUFFER_SIZE   257    // 256 characters + null terminator
#define DEFAULT_TEXT      "Hello!"
#define DEFAULT_FONT_IDX   1

// Padding for rectangular displays (all 4 sides)
#define TEXT_PADDING         3

// Character used in the config page as a manual line break
#define LINEBREAK_CHAR     '^'

// Round displays: vertical padding top/bottom in pixels.
// Determines how narrow the circle chord is at the tightest visible position,
// which in turn defines the maximum width of the text block.
#define ROUND_V_PADDING     60

// Horizontal position and width of the text block on round displays.
// Geometrically calculated so that at any scroll offset, all lines
// safely remain within the visible circular area.
// Derivation: Chord width = 2·sqrt(R²-(R-V_PAD)²) − 8px safety margin,
// x = (Display_w − width) / 2
//
//   Gabbro (R=130): Chord@y_mid=100 → 2·83.07−8 = 158px,  x=(260-158)/2 = 51
//   Chalk  (R=90):  Chord@y_mid=60  → 2·67.08−8 = 126px,  x=(180-126)/2 = 27
#ifdef PBL_PLATFORM_CHALK
  #define ROUND_TEXT_X    10
  #define ROUND_TEXT_W   160
#else
  #define ROUND_TEXT_X    25
  #define ROUND_TEXT_W   210
#endif

// Font index mapping:
//   0  ->  GOTHIC_18_BOLD   (small)
//   1  ->  GOTHIC_28_BOLD   (medium, default)
//   2  ->  BITHAM_30_BLACK  (large)
//   3  ->  BITHAM_42_BOLD   (very large)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer  *s_canvas_layer;

static char s_text_buf[TEXT_BUFFER_SIZE];
static int  s_font_idx;
static int  s_scroll_offset = 0;
static int  s_text_total_h  = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static GFont prv_get_font(int idx) {
  switch (idx) {
    case 0:  return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    case 2:  return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
    case 3:  return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    default: return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  }
}

// Replaces every circumflex '^' (0x5E) with a linefeed '\n' (0x0A).
// Same byte length, so this can be done in-place.
static void prv_replace_linebreak_char(char *s) {
  for (char *p = s; *p; p++) {
    if (*p == LINEBREAK_CHAR) {
      *p = '\n';
    }
  }
}

static int prv_line_height(void) {
  GFont font = prv_get_font(s_font_idx);
  // "Ag" contains ascenders and descenders -> returns the full line height
  GSize s = graphics_text_layout_get_content_size(
    "Ag", font,
    GRect(0, 0, 1000, 1000),
    GTextOverflowModeWordWrap, GTextAlignmentLeft
  );
  return s.h;
}

static int prv_max_scroll(void) {
  Layer *root = window_get_root_layer(s_window);
  int visible_h = layer_get_bounds(root).size.h;
#ifdef PBL_ROUND
  int visible_text_h = visible_h - (2 * ROUND_V_PADDING);
#else
  int visible_text_h = visible_h - (2 * TEXT_PADDING);
#endif
  int max = s_text_total_h - visible_text_h;
  return (max > 0) ? max : 0;
}

// Calculates the text height once when text or font changes (for performance).
// On round displays, the exact same static text layout bounds are used as during
// rendering, ensuring height and scroll limits are correct at any scroll offset.
// NO Screen Text Flow: Flow is screen-position dependent and would reflow the
// text differently at every single scroll offset.
static void prv_update_text_height(void) {
  if (!s_window) return;

  GFont font = prv_get_font(s_font_idx);

#ifdef PBL_ROUND
  GSize text_size = graphics_text_layout_get_content_size(
    s_text_buf, font,
    GRect(ROUND_TEXT_X, 0, ROUND_TEXT_W, 10000),
    GTextOverflowModeWordWrap, GTextAlignmentCenter
  );
#else
  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  int usable_width = bounds.size.w - (2 * TEXT_PADDING);
  GSize text_size = graphics_text_layout_get_content_size(
    s_text_buf, font,
    GRect(0, 0, usable_width, 10000),
    GTextOverflowModeWordWrap, GTextAlignmentLeft
  );
#endif

  s_text_total_h = text_size.h;
}

// ---------------------------------------------------------------------------
// Canvas layer
// ---------------------------------------------------------------------------

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font   = prv_get_font(s_font_idx);

  graphics_context_set_text_color(ctx, GColorBlack);

#ifdef PBL_ROUND
  // Static text block: width and x-position are geometrically chosen
  // so that all lines remain inside the visible circle at any scroll offset.
  // No text flow -> no reflow while scrolling.
  GRect draw_rect = GRect(
    ROUND_TEXT_X,
    ROUND_V_PADDING - s_scroll_offset,
    ROUND_TEXT_W,
    s_text_total_h
  );
  graphics_draw_text(ctx, s_text_buf, font, draw_rect,
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
#else
  // Rectangular display: fixed padding, left-aligned
  GRect draw_rect = GRect(
    TEXT_PADDING,
    TEXT_PADDING - s_scroll_offset,
    bounds.size.w - (2 * TEXT_PADDING),
    s_text_total_h
  );
  graphics_draw_text(ctx, s_text_buf, font, draw_rect,
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
#endif
}

// ---------------------------------------------------------------------------
// Button handler
// ---------------------------------------------------------------------------

static void prv_up_click(ClickRecognizerRef r, void *ctx) {
  s_scroll_offset -= prv_line_height();
  if (s_scroll_offset < 0) { s_scroll_offset = 0; }
  layer_mark_dirty(s_canvas_layer);
}

static void prv_down_click(ClickRecognizerRef r, void *ctx) {
  s_scroll_offset += prv_line_height();
  int max = prv_max_scroll();
  if (s_scroll_offset > max) { s_scroll_offset = max; }
  layer_mark_dirty(s_canvas_layer);
}

static void prv_select_click(ClickRecognizerRef r, void *ctx) {
  s_scroll_offset = 0;
  layer_mark_dirty(s_canvas_layer);
}

static void prv_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP,     200, prv_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN,   200, prv_down_click);
  window_single_repeating_click_subscribe(BUTTON_ID_SELECT, 500, prv_select_click);
}

// ---------------------------------------------------------------------------
// AppMessage
// ---------------------------------------------------------------------------

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t_text = dict_find(iter, MESSAGE_KEY_TEXT);
  if (t_text) {
    snprintf(s_text_buf, sizeof(s_text_buf), "%s", t_text->value->cstring);
    // Convert circumflex '^' into a real line break before saving
    prv_replace_linebreak_char(s_text_buf);
    persist_write_string(KEY_TEXT, s_text_buf);
  }

  Tuple *t_font = dict_find(iter, MESSAGE_KEY_FONT);
  if (t_font) {
    s_font_idx = atoi(t_font->value->cstring);
    persist_write_int(KEY_FONT, s_font_idx);
  }

  // Recalculate text height since content or font size has changed
  prv_update_text_height();

  // Always jump to the beginning of the text on new content
  s_scroll_offset = 0;
  layer_mark_dirty(s_canvas_layer);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void prv_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  window_set_background_color(window, GColorWhite);
  window_set_click_config_provider(window, prv_click_config);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update);
  layer_add_child(root, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

static void prv_init(void) {
  // Load persisted values or set defaults.
  // Call replacement here too, so that text from older builds (which still
  // contains '^') is automatically cleaned up at startup.
  if (persist_exists(KEY_TEXT)) {
    persist_read_string(KEY_TEXT, s_text_buf, sizeof(s_text_buf));
    prv_replace_linebreak_char(s_text_buf);
  } else {
    snprintf(s_text_buf, sizeof(s_text_buf), "%s", DEFAULT_TEXT);
  }
  s_font_idx = persist_exists(KEY_FONT)
             ? persist_read_int(KEY_FONT)
             : DEFAULT_FONT_IDX;

  // Open AppMessage: Inbox large enough for 256 characters of text + overhead
  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(512, 64);

  // Create and display window
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });

  // Calculate initial text height as soon as the window object exists
  prv_update_text_height();

  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}