/**
 * @file RS485Config.h
 * @brief Hardware configuration for RS485 communication with MikroElektronika RS485 5 Click
 * @details Uses FOD817BSD optical signal isolation between ESP32 (3.3V) and RS485 module (5V)
 * @details Hardware-managed LDO architecture with always-on power supplies and common ground
 * @author Paul Waserbrot
 * @date 2025-06-09
 */

#ifndef WATERINGSYSTEM_HARDWARE_RS485CONFIG_H
#define WATERINGSYSTEM_HARDWARE_RS485CONFIG_H

/**
 * @brief RS485 Hardware Configuration - Hardware-Managed LDO System
 * @details LDO-based solution using FOD817BSD optical signal isolation only
 * ESP32 (3.3V LDO always-on) ↔ FOD817BSD ↔ RS485/sensor circuits (5V LDO always-on)
 * Common ground design with optical signal isolation for RS485 communication
 */

// Hardware identification
#define RS485_MODULE_NAME "MikroElektronika RS485 5 Click"
#define RS485_IC_NAME "ADM3485ARZ"
#define RS485_ISOLATION_IC "FOD817BSD"
#define RS485_PART_NUMBER "MIKROE-4156"

// Pin definitions for ESP32 side (3.3V LDO always-on)
#define RS485_TX_PIN 16                 // ESP32 TX -> FOD817BSD -> RS485 DI
#define RS485_RX_PIN 17                 // ESP32 RX <- FOD817BSD <- RS485 RO
#define RS485_DE_RE_PIN 25              // Driver/Receiver Enable via optocoupler
// Note: No power enable pin needed - RS485 module always powered by 5V LDO

// RS485 Bus Configuration
#define RS485_BAUD_RATE 9600           // Standard Modbus RTU baud rate
#define RS485_DATA_BITS 8              // 8 data bits
#define RS485_STOP_BITS 1              // 1 stop bit
#define RS485_PARITY SERIAL_8N1        // No parity

// Hardware-managed LDO system - always-on operation
#define RS485_HAS_BUILTIN_ISOLATION false  // No ground isolation (common ground design)
#define RS485_CUSTOM_GROUND_ISOLATION false // Not required for LDO common ground system
#define RS485_GROUND_ISOLATION_METHOD "OPTICAL_SIGNAL_ISOLATION_ONLY"

// Hardware-managed timing constants (LDO always-on system)
#define RS485_DE_ASSERT_DELAY_US 50    // Standard delay for direction control
#define RS485_DE_DEASSERT_DELAY_US 50  // Standard delay for direction control
#define RS485_RX_TIMEOUT_MS 1000       // Receive timeout
#define RS485_POWER_STABILIZE_MS 0     // No stabilization needed (always-on LDO)
#define RS485_DEFAULT_TIMEOUT_MS 3000  // Standard timeout for communication
#define RS485_POWER_ON_DELAY_MS 0      // No power-on delay (always-on LDO)

// Optical isolation parameters
#define OPTOCOUPLER_FORWARD_CURRENT_MA 10    // Forward current for FOD817BSD
#define OPTOCOUPLER_PULLUP_RESISTOR_OHMS 1000 // Pull-up resistor on collector
#define OPTOCOUPLER_CURRENT_LIMIT_OHMS 220    // Current limiting resistor for LED

// Bus termination and protection
#define RS485_TERMINATION_OHMS 120     // Bus termination resistance
#define RS485_ESD_PROTECTION_KV 15     // ESD protection level (±15kV)
#define RS485_ISOLATION_VOLTAGE_VRMS 5000  // FOD817BSD isolation voltage (PRIMARY ONLY)

// Hardware-managed LDO system specifications
// No complex ground isolation components needed
// AMS1117 LDO regulators provide stable always-on power

// Total system isolation specifications - SIGNAL ISOLATION ONLY
#define TOTAL_SYSTEM_ISOLATION_KV 5.0    // FOD817BSD optical signal isolation only
#define GROUND_ISOLATION_METHOD_DESC "Common ground with optical signal isolation"
#define SAFETY_CLASSIFICATION "Hardware_LDO_Managed_Always_On"

// Power specifications - HARDWARE-MANAGED LDO
#define ESP32_DOMAIN_VOLTAGE_V 3.3     // ESP32 and control logic voltage (AMS1117-3.3)
#define FIELD_DOMAIN_VOLTAGE_V 5.0     // Field domain for RS485 and sensors (AMS1117-5.0)

// Safety and compliance - COMMON GROUND DESIGN
#define RS485_GALVANIC_ISOLATION false // No galvanic isolation (common ground)
#define RS485_SIGNAL_ISOLATION true    // Optical signal isolation enabled
#define RS485_GROUND_LOOP_PROTECTION false // Not needed with common ground

/**
 * @brief Hardware feature flags - LDO-MANAGED SYSTEM
 */
#define RS485_HAS_OPTICAL_ISOLATION 1      // Signal isolation via FOD817BSD
#define RS485_HAS_ESD_PROTECTION 1         // ESD protection on RS485 module
#define RS485_HAS_SEPARATE_POWER_DOMAINS 0 // Common ground design (LDO-managed)
#define RS485_HAS_ALWAYS_ON_POWER 1        // Always-on LDO power supplies

/**
 * @brief Connection verification for LDO-managed system
 * These defines help verify correct hardware connections
 */
#define RS485_EXPECTED_MODULE "RS485 5 Click"
#define RS485_EXPECTED_ISOLATION "FOD817BSD"
#define RS485_MIN_ISOLATION_VOLTAGE 2500 // Minimum isolation voltage (V)
#define RS485_EXPECTED_POWER_SUPPLY "AMS1117-5.0" // Expected 5V LDO regulator

#endif // WATERINGSYSTEM_HARDWARE_RS485CONFIG_H
