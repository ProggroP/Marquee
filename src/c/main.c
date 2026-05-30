#include <pebble.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define KEY_TEXT             0
#define KEY_FONT             1
#define TEXT_BUFFER_SIZE   257    // 256 Zeichen + Null-Terminator
#define DEFAULT_TEXT      "Hello!"
#define DEFAULT_FONT_IDX   1

// Konfigurierbarer Abstand an allen 4 Seiten (in Pixeln)
#define TEXT_PADDING         3

// Font index mapping:
//   0  ->  GOTHIC_18_BOLD   (klein)
//   1  ->  GOTHIC_28_BOLD   (mittel, default)
//   2  ->  BITHAM_30_BLACK  (groß)
//   3  ->  BITHAM_42_BOLD   (sehr groß)

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

static int prv_line_height(void) {
  GFont font = prv_get_font(s_font_idx);
  // "Ag" enthält Ober- und Unterlängen -> liefert die volle Zeilenhöhe
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
  
  // Sichtbare Höhe abzüglich des oberen und unteren Paddings
  int visible_text_h = visible_h - (2 * TEXT_PADDING);
  int max = s_text_total_h - visible_text_h;
  
  return (max > 0) ? max : 0;
}

// Berechnet die Texthöhe einmalig bei Text- oder Schriftänderung (Performance)
static void prv_update_text_height(void) {
  if (!s_window) return;
  
  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  GFont font   = prv_get_font(s_font_idx);
  
  // Verfügbare Breite abzüglich des linken und rechten Paddings
  int usable_width = bounds.size.w - (2 * TEXT_PADDING);
  
  GSize text_size = graphics_text_layout_get_content_size(
    s_text_buf, font, GRect(0, 0, usable_width, 10000),
    GTextOverflowModeWordWrap, GTextAlignmentLeft
  );
  s_text_total_h = text_size.h;
}

// ---------------------------------------------------------------------------
// Canvas layer
// ---------------------------------------------------------------------------

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font   = prv_get_font(s_font_idx);

  // Zeichenrechteck berücksichtigt das Padding und den aktuellen Scroll-Offset
  GRect draw_rect = GRect(
    TEXT_PADDING, 
    TEXT_PADDING - s_scroll_offset, 
    bounds.size.w - (2 * TEXT_PADDING), 
    s_text_total_h
  );

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(
    ctx, s_text_buf, font, draw_rect,
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL
  );
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
    persist_write_string(KEY_TEXT, s_text_buf);
  }

  Tuple *t_font = dict_find(iter, MESSAGE_KEY_FONT);
  if (t_font) {
    s_font_idx = atoi(t_font->value->cstring);
    persist_write_int(KEY_FONT, s_font_idx);
  }

  // Texthöhe neu berechnen, da sich Inhalt oder Schriftgröße geändert haben
  prv_update_text_height();

  // Bei neuem Inhalt immer zum Textanfang springen
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
  // Persistierte Werte laden oder Defaults setzen
  if (persist_exists(KEY_TEXT)) {
    persist_read_string(KEY_TEXT, s_text_buf, sizeof(s_text_buf));
  } else {
    snprintf(s_text_buf, sizeof(s_text_buf), "%s", DEFAULT_TEXT);
  }
  s_font_idx = persist_exists(KEY_FONT)
             ? persist_read_int(KEY_FONT)
             : DEFAULT_FONT_IDX;

  // AppMessage öffnen: Inbox gross genug für 256 Zeichen Text + Overhead
  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(512, 64);

  // Fenster erzeugen und anzeigen
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });

  // Initiale Texthöhe berechnen, sobald das Window-Objekt existiert
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
