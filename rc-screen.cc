/*
 * remote controlled screen.
 * (c) 2010 h.zeller@acm.org
 * This is free softare. Apache license.
 *
 * Attempt to use C++ for better encapsulation and avoid ugly 'object' structs.
 * And it actually seems that code size is pretty comparable compared to pure C.
 * Thanks GCC!
 *
 * Our inputs
 *  - A7/ICP: IR receiver input from TSOP 38. Connected to A7/ICP. Low active.
 *  - AIN0  : Wheel encoder. Analog input comparing to AIN1. AIN1 is a voltage
 *            divider biased by another output of ours -> Schmitt trigger.
 *  - B1    : End-switch, active low.
 *
 * Outputs
 *  - B0/A6 : Motor up/down (connected to H-Bridge)
 *  - A0    : Schmitt Trigger bias
 *  - A4    : Debug LED.
 *
 * TODO
 - 'program' low position.
*/

#define AVR_MHZ 8
#define F_CPU (AVR_MHZ * 1000000UL)

#include <avr/io.h>
#include <avr/interrupt.h>

typedef unsigned char byte_t;

// -- Used ports. Named {IN,OUT}_[name]_[io-portname]
enum {
  IN_ENDSWITCH_B  = (1<<1),  // Endswitch, active low.
  IN_IR_A         = (1<<7),  // Infrared receiver. Idle high.
  IN_RESET_B      = (1<<3),  // To set the pullup.

  OUT_STATUSLED_A = (1<<4),  // Some LED. Lit on high.
  OUT_MOT_DN_A    = (1<<6),  // H-bridge #1
  OUT_MOT_UP_B    = (1<<0),  // H-bridge #2
  OUT_STBIAS_A    = (1<<0),  // Schmitt-Trigger bias voltage.
};

// Switch Status LED.
static void status_led(bool b) {
  if (b)
    PORTA |= OUT_STATUSLED_A;
  else
    PORTA &= ~OUT_STATUSLED_A;
}

static inline bool infrared_in() { return (PINA & IN_IR_A) != 0; }
static inline bool endswitch_in() {
  // active low which will return true.
  return (PINB & IN_ENDSWITCH_B) == 0;
}

namespace Clock {
  typedef unsigned short cycle_t;
    
  static void init() {
    TCCR1B = (1<<CS12) | (1<<CS10);  // clk/1024
  }

  // The timer with aroud 7.8 kHz rolls over the 64k every 8.3 seconds: it
  // makes only sense to do unsigned time comparisons <= 8.3 seconds.
  // Returns clock ticks.
  static cycle_t now() { return TCNT1; }

  // Converts milliseconds into clock cycles. If you provide a constant
  // expression, the compiler will be able to replace this with a constant,
  // otherwise it'll get expensive (division and such).
  static cycle_t ms_to_cycles(unsigned short ms) {
    return ms * (F_CPU / 1024/*prescaler*/) / 1000/*ms*/;
  }
} // end namespace Clock

class Screen {
private:
  static const short SCREEN_UP_STOP_THRESHOLD =  -4;
  static const short SCREEN_DN_STOP_THRESHOLD = 258;

public:
  enum Direction {
    DIR_NEUTRAL,
    DIR_UP,
    DIR_DOWN
  };
  enum ErrorType {
    ERR_NONE,
    ERR_SWITCH,
    ERR_ROTATION
  };
  Screen() : error_(ERR_NONE), motor_dir_(DIR_NEUTRAL), pos_(0) {
    /* nop */
  }
  
  // Set motor to given direction, but honor endpositions.
  void set_dir(Direction d) {
    if (d == motor_dir_)
      return;
    // We're connected to two output ports for layout reasons, hence two
    // IO-operations per direction. We only switch the motor on if we're
    // within limits.
    switch (d) {
    case DIR_UP:
      if (!up_stop_condition()) {
        PORTA &= ~OUT_MOT_DN_A;
        PORTB |=  OUT_MOT_UP_B;
        motor_dir_ = d;
        last_update_time_ = Clock::now();
      }
      break;
    case DIR_DOWN:
      if (!down_stop_condition()) {
        PORTA |=  OUT_MOT_DN_A;
        PORTB &= ~OUT_MOT_UP_B;
        motor_dir_ = d;
        last_update_time_ = Clock::now();
      }
      break;
    default:
      PORTA &= ~OUT_MOT_DN_A;
      PORTB &= ~OUT_MOT_UP_B;
      motor_dir_ = d;
    }
  }

  // Set the screen in motion towards home position if not already reached.
  // Resets any error state.
  void go_home() {
    if (error_ == ERR_SWITCH)
      return;  // We know this guy is faulty. Don't do anything.
    error_ = ERR_NONE;
    if (endswitch_in())
      return;  // already there.
    // We don't know where we are, but at most we're going the full
    // length of the screen. In this case we actually rely on the endswitch
    // working properly because the negative position check might be late.
    pos_ = SCREEN_DN_STOP_THRESHOLD;
    set_dir(DIR_UP);
  }

  // Outside event: a tick from the rotation encoder. Updates position.
  void event_rotation_tick() volatile {
    last_update_time_ = Clock::now();
    switch (motor_dir_) {
    case DIR_UP:
      --pos_;
      break;
    case DIR_DOWN:
      ++pos_;
      break;
    default:
      ;
    }
  }

  // Outside event: endswitch is triggered. Updates position.
  void event_endswitch_triggered()  {
    if (motor_dir_ != DIR_DOWN) {
      pos_ = 0;
    }
  }

  // Check stop conditions and stops motor if so.
  // This method must be called regularly.
  void check_stop_conditions() {
    if (motor_dir_ != DIR_NEUTRAL
        && Clock::now() - last_update_time_ > Clock::ms_to_cycles(1000)) {
      // Wheel encoder failed or motor stuck: haven't received
      // a tick for some time.
      enter_error_state(ERR_ROTATION);
    }
    if (motor_dir_ == DIR_UP && pos_ <= SCREEN_UP_STOP_THRESHOLD) {
      // Endswitch failed. We're up beyond home position.
      enter_error_state(ERR_SWITCH);
    }

    if ((motor_dir_ == DIR_UP && up_stop_condition())
        || (motor_dir_ == DIR_DOWN && down_stop_condition())) {
      set_dir(DIR_NEUTRAL);
    }
  }

  inline ErrorType error() const { return error_; }

private:
  inline bool up_stop_condition() {
    return error_ || pos_ <= SCREEN_UP_STOP_THRESHOLD || endswitch_in();
  }

  inline bool down_stop_condition() {
    return error_ || pos_ >= SCREEN_DN_STOP_THRESHOLD;
  }

  void enter_error_state(ErrorType type) {
    set_dir(DIR_NEUTRAL);
    error_ = type;
  }

  ErrorType error_;
  Direction motor_dir_;
  volatile short pos_;  // volatile, because updated in ISR.    
  volatile Clock::cycle_t last_update_time_;
};

static volatile Screen *global_screen; // ISR needs to access the screen.
ISR(ANA_COMP_vect) {
  static bool last = 0;
  const bool got_falling_edge = (ACSR & (1<<ACO)) == 0;
  if (got_falling_edge != last) {
    last = got_falling_edge;
    global_screen->event_rotation_tick();
  }

  // Schmitt-Trigger bias.
  if (got_falling_edge) {
    PORTA |= OUT_STBIAS_A;  // just had a falling edge: apply positive bias.
  } else {
    PORTA &= ~OUT_STBIAS_A;  // just gut a raising edge: apply neg. bias.
  }
}

static byte_t read_infrared(byte_t *buffer) {
  // The infrared input is default high.
  // A transmission starts with a long low phase (which triggered us to
  // be in this routine in the first place), followed by a sequence of bits
  // that are encoded in the duration of the high-phases. We interpret that as
  // long == 1, short == 0. The end of the signal is reached once we see the
  // high phase to be overly long (or: when 4 bytes are read).
  // The timings were determined empirically.
  byte_t read = 0;
  byte_t current_bit = 0x80;
  *buffer = 0;

  // 8Mhz measurement: min=539 ; max=1500 in-between=1019 -> 127/Mhz
  const unsigned short lo_hi_bit_threshold = 127 * AVR_MHZ;
  const unsigned short end_of_signal = 1500 * AVR_MHZ;
  unsigned short count = 0;

  while (read < 4) {
    while (!infrared_in()) {}  // skip low phase, wait for high.
    for (count = 0; infrared_in() && count < end_of_signal; ++count)
      ;
    if (count >= end_of_signal)
      break; // we're done - final high state.
    if (count > lo_hi_bit_threshold) {
      *buffer |= current_bit;
    }
    current_bit >>= 1;
    if (!current_bit) {
      current_bit = 0x80;
      ++read;
      if (read == 4)
        break;
      ++buffer;
      *buffer = 0;
    }
  }
  return read;
}

enum Button {     // Infrared signal:
  BUTTON_ON,    //  E0 D5 04 FB
  BUTTON_OFF,   //  E0 D5 44 BB
  BUTTON_UP,    //  E0 D5 06 F9
  BUTTON_DOWN,  //  E0 D5 26 D9
  BUTTON_SET,   //  E0 D5 50 AF
  BUTTON_UNKNOWN
};

// Decode infrared signal and return matching Screen direction commands.
static Button DecodeInfrared(byte_t *buffer) {
  if (buffer[0] != 0xE0 && buffer[1] != 0xD5)
    return BUTTON_UNKNOWN;
  const byte_t u = buffer[2];
  const byte_t l = buffer[3];
  if (u == 0x04 && l == 0xFB) return BUTTON_ON;
  if (u == 0x44 && l == 0xBB) return BUTTON_OFF;
  if (u == 0x06 && l == 0xF9) return BUTTON_UP;
  if (u == 0x26 && l == 0xD9) return BUTTON_DOWN;
  if (u == 0x50 && l == 0xAF) return BUTTON_SET;
  return BUTTON_UNKNOWN;
}

class Monoflop {
public:
  Monoflop(Clock::cycle_t cycles)
    : duration_(cycles), trigger_time_(0), active_(false) {
  }

  void trigger() {
    trigger_time_ = Clock::now();
    active_ = true;
  }

  bool is_active() { return active_; }

  // This check has to be called regularly somewhere in the main-loop.
  void regular_check() {
    if (active_ && Clock::now() - trigger_time_ >= duration_) {
      active_ = false;
    }
  }

private:
  const Clock::cycle_t duration_;
  Clock::cycle_t trigger_time_;
  bool active_;
};

// We react on the on/off buttons to move the screen.
// After these buttons have been pressed, it is possible as well to use the
// up/down buttons on the remote control to control the screen.
// Sometimes we want to control the screen but not affect the projector. So we
// can press first the button that is idempotent wrt. the state of the projector
// (i.e. press the 'on' button when it is already on) - afterwards we can use
// the up/down buttons to send the actual command we want.
// We need to make this time limited, because the user might use the remote
// control to actually operate the projector - in that case we don't want to
// have the screen move up/down while using these buttons :)
static void handle_infrared(Monoflop *extra_buttons_active, Screen *screen) {
  byte_t infrared_bytes[4] = {0, 0, 0, 0};
  if (read_infrared(infrared_bytes) != 4)
    return;
  switch (DecodeInfrared(infrared_bytes)) {
  case BUTTON_ON:
    if (screen->error() == Screen::ERR_ROTATION) {
      screen->go_home();
    } else {
      screen->set_dir(Screen::DIR_DOWN);
      extra_buttons_active->trigger();
    }
    break;
  case BUTTON_OFF:
    screen->set_dir(Screen::DIR_UP);
    extra_buttons_active->trigger();
    break;
  case BUTTON_UP:
    screen->set_dir(extra_buttons_active->is_active()
                    ? Screen::DIR_UP : Screen::DIR_NEUTRAL);
    break;
  case BUTTON_DOWN:
    screen->set_dir(extra_buttons_active->is_active()
                    ? Screen::DIR_DOWN : Screen::DIR_NEUTRAL);
    break;
  case BUTTON_UNKNOWN:
    screen->set_dir(Screen::DIR_NEUTRAL);
    break;
  case BUTTON_SET:  break;  // ignored for now.
  }
}

int main(void) {
  // Outputs.
  DDRA = OUT_STATUSLED_A | OUT_MOT_DN_A | OUT_STBIAS_A;
  DDRB = OUT_MOT_UP_B;

  // Switch on pullups.
  PORTB = IN_RESET_B | IN_ENDSWITCH_B;

  // Don't need digital input buffer.
  DIDR0 = (1<<ADC2D) | (1<<ADC1D);

  Clock::init();

  // Init screen. Need to assign to global_screen before interrupt enable.
  Screen screen;
  global_screen = &screen;  // referenced in interrupt handler.

  Monoflop extra_buttons_active(Clock::ms_to_cycles(4000));

  // Enable comparator interrupt. It will give us the rotation tick events.
  ACSR |= (1<<ACIE);
  sei();

  // until we know that the endswitch is working we keep this switched off.
  // We assume home position
  screen.go_home();

  for (;;) {
    screen.check_stop_conditions();
    extra_buttons_active.regular_check();

    if (!infrared_in()) {  // an IR transmission starts with low.
      handle_infrared(&extra_buttons_active, &screen);
    }
    if (endswitch_in()) {
      screen.event_endswitch_triggered();
    }
    // LED output depends on the state of the screen
    switch (screen.error()) {
    case Screen::ERR_NONE:
      status_led(extra_buttons_active.is_active());
      break;
    case Screen::ERR_SWITCH:
      status_led(Clock::now() & 4096);  // slow blink.
      break;
    case Screen::ERR_ROTATION:
      status_led(Clock::now() & 1024);  // fast blink.
      break;
    }
  }
  return 0;  // not reached.
}
