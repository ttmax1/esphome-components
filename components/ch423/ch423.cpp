#include "ch423.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ch423 {

static const uint8_t CH423_REG_MODE = 0x24;
static const uint8_t CH423_MODE_OUTPUT = 0x01;      // enables output mode on 0-7
static const uint8_t CH423_MODE_OPEN_DRAIN = 0x04;  // enables open drain mode on 8-11
static const uint8_t CH423_REG_IN = 0x26;           // read reg for input bits
static const uint8_t CH423_REG_OUT = 0x38;          // write reg for output bits 0-7
static const uint8_t CH423_REG_OUT_UPPER = 0x23;    // write reg for output bits 8-11
static const uint8_t CH423_REG_OUT_UPPER_l = 0x22;    // write reg for output bits 8-15

static const char *const TAG = "ch423";

void CH423Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up CH423...");
  // set outputs before mode
  this->write_outputs_();
  // Set mode and check for errors
  if (!this->set_mode_(this->mode_value_) || !this->read_inputs_()) {
    ESP_LOGE(TAG, "CH423 not detected at 0x%02X", this->address_);
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "Initialization complete. Warning: %d, Error: %d", this->status_has_warning(),
                this->status_has_error());
}

void CH423Component::loop() {
  // Clear all the previously read flags.
  this->pin_read_flags_ = 0x00;
}

void CH423Component::dump_config() {
  ESP_LOGCONFIG(TAG, "CH423:");
  LOG_I2C_DEVICE(this)
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Communication with CH423 failed!");
  }
}

void CH423Component::pin_mode(uint8_t pin, gpio::Flags flags) {
  if (pin < 8) {
    if (flags & gpio::FLAG_OUTPUT) {
      this->mode_value_ |= CH423_MODE_OUTPUT;
    }
  } else {
    if (flags & gpio::FLAG_OPEN_DRAIN) {
      this->mode_value_ |= CH423_MODE_OPEN_DRAIN;
    }
  }
}

bool CH423Component::digital_read(uint8_t pin) {
  if (this->pin_read_flags_ == 0 || this->pin_read_flags_ & (1 << pin)) {
    // Read values on first access or in case it's being read again in the same loop
    this->read_inputs_();
  }

  this->pin_read_flags_ |= (1 << pin);
  return (this->input_bits_ & (1 << pin)) != 0;
}

void CH423Component::digital_write(uint8_t pin, bool value) {
  if (value) {
    this->output_bits_ |= (1 << pin);
  } else {
    this->output_bits_ &= ~(1 << pin);
  }
  this->write_outputs_();
}

bool CH423Component::read_inputs_() {
  if (this->is_failed()) {
    return false;
  }
  uint8_t result;
  // reading inputs requires the chip to be in input mode, possibly temporarily.
  if (this->mode_value_ & CH423_MODE_OUTPUT) {
    this->set_mode_(this->mode_value_ & ~CH423_MODE_OUTPUT);
    result = this->read_reg_(CH423_REG_IN);
    this->set_mode_(this->mode_value_);
  } else {
    result = this->read_reg_(CH423_REG_IN);
  }
  this->input_bits_ = result;
  this->status_clear_warning();
  return true;
}

// Write a register. Can't use the standard write_byte() method because there is no single pre-configured i2c address.
bool CH423Component::write_reg_(uint8_t reg, uint8_t value) {
  auto err = this->bus_->write(reg, &value, 1);
  if (err != i2c::ERROR_OK) {
    this->status_set_warning(str_sprintf("write failed for register 0x%X, error %d", reg, err).c_str());
    return false;
  }
  this->status_clear_warning();
  return true;
}

uint8_t CH423Component::read_reg_(uint8_t reg) {
  uint8_t value;
  auto err = this->bus_->read(reg, &value, 1);
  if (err != i2c::ERROR_OK) {
    this->status_set_warning(str_sprintf("read failed for register 0x%X, error %d", reg, err).c_str());
    return 0;
  }
  this->status_clear_warning();
  return value;
}

bool CH423Component::set_mode_(uint8_t mode) { return this->write_reg_(CH423_REG_MODE, mode); }

bool CH423Component::write_outputs_() {
  return this->write_reg_(CH423_REG_OUT, static_cast<uint8_t>(this->output_bits_)) &&
         this->write_reg_(CH423_REG_OUT_UPPER_l, static_cast<uint8_t>(this->output_bits_ >> 8))&&
         this->write_reg_(CH423_REG_OUT_UPPER, static_cast<uint8_t>(this->output_bits_ >> 16));
}

float CH423Component::get_setup_priority() const { return setup_priority::IO; }

// Run our loop() method very early in the loop, so that we cache read values
// before other components call our digital_read() method.
float CH423Component::get_loop_priority() const { return 9.0f; }  // Just after WIFI

void CH423GPIOPin::pin_mode(gpio::Flags flags) { this->parent_->pin_mode(this->pin_, flags); }
bool CH423GPIOPin::digital_read() { return this->parent_->digital_read(this->pin_) ^ this->inverted_; }

void CH423GPIOPin::digital_write(bool value) { this->parent_->digital_write(this->pin_, value ^ this->inverted_); }
std::string CH423GPIOPin::dump_summary() const { return str_sprintf("EXIO%u via CH423", pin_); }
void CH423GPIOPin::set_flags(gpio::Flags flags) {
  flags_ = flags;
  this->parent_->pin_mode(this->pin_, flags);
}

}  // namespace ch423
}  // namespace esphome
