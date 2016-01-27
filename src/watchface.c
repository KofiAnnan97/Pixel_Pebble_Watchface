#include <pebble.h>

#define KEY_TEMPERATURE 0

#define HAND_MARGIN 10
#define FINAL_RADIUS 55
#define ANIMATION_DURATION 500
#define ANIMATION_DELAY 600

typedef struct{
  int hours;
  int minutes;
} Time;

static Window *s_main_window;
static TextLayer *s_date_layer,
                 *s_day_layer,
                 *s_weather_layer;
static BitmapLayer *s_background_layer,
                   *s_bluetooth_layer,
                   *s_battery_layer;
static GBitmap *s_background_bitmap,
               *s_bluetooth_bitmap,
               *s_battery_bitmap;
static Layer *s_canvas_layer;

GFont pixel_font;

static int s_battery_level;
int disconnect_count = 0;

GPoint s_center;
static Time s_last_time, s_anim_time;
static int s_radius = 0;
static bool s_animating = false;

static void inbox_received_callback(DictionaryIterator *iterator, void *context){
  static char temperature_buffer[8];

  Tuple *temp_tuple = dict_find( iterator, KEY_TEMPERATURE);

  snprintf(temperature_buffer, sizeof(temperature_buffer), "%dF", (int)temp_tuple->value->int32);
  text_layer_set_text(s_weather_layer, temperature_buffer);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context){
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message Dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context){
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context){
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send success!");
}

static void animation_started(Animation *anim, void *context){
  s_animating = true;
}

static void animation_stopped(Animation *anim, bool stoppeed, void *context){
  s_animating = false;
}

static void animate(int duration, int delay, AnimationImplementation *implementation, bool handlers){
  Animation *anim = animation_create();
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, implementation);
  if(handlers){
    animation_set_handlers(anim, (AnimationHandlers){
      .started = animation_started,
      .stopped = animation_stopped
    }, NULL);
  }
  animation_schedule(anim);
}

//Determines what battery image will be displayed on the watch
static void battery_indicator(BatteryChargeState charge_state){
  s_battery_level = charge_state.charge_percent;
  //work on integrating charged battery for when battery is fully charged but still  plugged in
      
     //get Full battery picture
    /*if( charge_state.is_charging){
        s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_CHARGED_Battery);
    } else*/ if(s_battery_level >= 67){
        s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_FULL_Battery);
      
    } else if(s_battery_level >= 34 && s_battery_level <= 66){
        //get 67% battery picture
        s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_67_Battery);
    } else if(s_battery_level >= 11 && s_battery_level <= 33){
        //get 33% battery picture
        s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_33_Battery);
    } else if(s_battery_level <= 10){
        //get Low battery picture
        s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_LOW_Battery);
    }
  //}
  bitmap_layer_set_bitmap(s_battery_layer, s_battery_bitmap);
}

//determines whether the bluetooth is on 
static void bluetooth_callback(bool connected){
  
  layer_set_hidden(bitmap_layer_get_layer(s_bluetooth_layer), connected);
  layer_set_hidden(text_layer_get_layer(s_weather_layer), !connected);
  
  if(!connected){
     vibes_double_pulse();
  }  
}

static int hours_to_minutes(int hours_out_of_12) {
  return (int)(float)(((float)hours_out_of_12 / 12.0F) * 60.0F);
}

static void update_proc(Layer *layer, GContext *ctx) {
 
  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_context_set_stroke_width(ctx, 2);

  Time mode_time = (s_animating) ? s_anim_time : s_last_time;
  
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle;
  if(s_animating) {
    // Hours out of 60 for smoothness
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 60;
  } else {
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;
  }
  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius -  HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.y,
  };
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(s_radius - (2.5 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(s_radius - (2.5 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.y,
  };

  // Draw hands with positive length only
  if(s_radius > 2 * HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, hour_hand);
  }
  if(s_radius > HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, minute_hand);
  }
}

static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(float)(((float)dist_normalized / (float)ANIMATION_NORMALIZED_MAX) * (float)max);
}

static void radius_update(Animation *anim, AnimationProgress dist_normalized) {
  s_radius = anim_percentage(dist_normalized, FINAL_RADIUS);

  layer_mark_dirty(s_canvas_layer);
}

static void hands_update(Animation *anim, AnimationProgress dist_normalized) {
  s_anim_time.hours = anim_percentage(dist_normalized, hours_to_minutes(s_last_time.hours));
  s_anim_time.minutes = anim_percentage(dist_normalized, s_last_time.minutes);

  layer_mark_dirty(s_canvas_layer);
}

static void update_time(){
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  static char date_buffer[8];
  strftime(date_buffer, sizeof(date_buffer), "%d %b", tick_time);

  static char day_buffer[6];
  strftime(day_buffer, sizeof(day_buffer), "%a", tick_time);

  text_layer_set_text(s_date_layer, date_buffer);
  text_layer_set_text(s_day_layer, day_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed){
  update_time();

  s_last_time.hours = tick_time->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = tick_time->tm_min;

  if(s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
  
  if(tick_time->tm_min % 30 == 0){
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    dict_write_uint8(iter, 0, 0);

    app_message_outbox_send();
  }
}

static void main_window_load(Window *window){
  //Creating initial window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  //Adding the background image
  s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_pixel_face);
  s_background_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_background_layer));

  s_center = grect_center_point(&GRect(71,98,2,2));
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, update_proc);
  layer_add_child(window_layer, s_canvas_layer);
  
  //Normal font choice --> fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD
  pixel_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_Pixel_Letters_Full_24));
  
  
  //Adding date (DD/MM)
  s_date_layer = text_layer_create(GRect(70, 0, 70, 25));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, pixel_font);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
 
  //Adding name of specific day
  s_day_layer = text_layer_create(GRect(5, 0, 40, 25));
  text_layer_set_background_color(s_day_layer, GColorClear);
  text_layer_set_text_color(s_day_layer, GColorWhite);
  text_layer_set_font(s_day_layer, pixel_font);
  layer_add_child(window_layer, text_layer_get_layer(s_day_layer));
  
  //Adding battery image
  s_battery_layer = bitmap_layer_create(GRect(3, 150, 32, 15));
  bitmap_layer_set_background_color(s_battery_layer, GColorBlue);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_battery_layer));

  
  //Adding bluetooth image
  s_bluetooth_bitmap = gbitmap_create_with_resource(RESOURCE_ID_LOST_Bluetooth_Signal);
  s_bluetooth_layer = bitmap_layer_create(GRect(115, 140, 25, 25));
  bitmap_layer_set_bitmap(s_bluetooth_layer, s_bluetooth_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_bluetooth_layer));


  //Displays temerature of geographic location (fahrenheit) 
  s_weather_layer = text_layer_create(GRect(108, 140, 35, 25));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, GColorWhite);
  text_layer_set_text(s_weather_layer, "...");
  text_layer_set_font(s_weather_layer, pixel_font);
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_weather_layer));
}

//Removing layers when no longer needed
static void main_window_unload(Window *window){
  layer_destroy(s_canvas_layer);
  
  gbitmap_destroy(s_background_bitmap);
  bitmap_layer_destroy(s_background_layer);
  
  text_layer_destroy(s_date_layer);
  
  text_layer_destroy(s_day_layer);
  
  fonts_unload_custom_font(pixel_font);
  
  gbitmap_destroy(s_battery_bitmap);
  bitmap_layer_destroy(s_battery_layer);
  
  gbitmap_destroy(s_bluetooth_bitmap);
  bitmap_layer_destroy(s_bluetooth_layer);
  
  text_layer_destroy(s_weather_layer);
  
}

static void init(){
  s_main_window = window_create();

  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_main_window, true);

  battery_indicator(battery_state_service_peek());
  battery_state_service_subscribe(battery_indicator);

  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = bluetooth_callback
  });

  bluetooth_callback(connection_service_peek_pebble_app_connection());

  update_time();

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  AnimationImplementation radius_impl = {
    .update = radius_update
  };
  animate(ANIMATION_DURATION, ANIMATION_DELAY, &radius_impl, false);

  AnimationImplementation hands_impl = {
    .update = hands_update
  };
  animate(2 * ANIMATION_DURATION, ANIMATION_DELAY, &hands_impl, true);
  
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit(){
  window_destroy(s_main_window);
}

int main(void){
  init();
  app_event_loop();
  deinit();

}
