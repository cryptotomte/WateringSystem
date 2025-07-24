/**
 * @file RS485Config.h
 * @brief Hardware configuration for RS485 communication with MikroElektronika RS485 5 Click
 * @details Uses TXS0108E bidirectional level shifter between ESP32 (3.3V) and RS485 module (5V)
 * @details Hardware-managed LDO architecture with always-on power supplies and common ground
 * @author Paul Waserbrot
 * @date 2025-06-09
 * @updated 2025-06-11 - Changed from FOD817BSD optocoupler to TXS0108E level shifter
 */

#ifndef WATERINGSYSTEM_HARDWARE_RS485CONFIG_H
#define WATERINGSYSTEM_HARDWARE_RS485CONFIG_H

/**
 * @brief RS485 Hardware Configuration - Hardware-Managed LDO System
 * @details LDO-based solution using TXS0108E bidirectional level shifter
 * ESP32 (3.3V LDO always-on) ↔ TXS0108E ↔ RS485/sensor circuits (5V LDO always-on)
 * Common ground design with level shifting for RS485 communication
 */

// Hardware identification
#define RS485_MODULE_NAME "MikroElektronika RS485 5 Click"
#define RS485_IC_NAME "ADM3485ARZ"
#define RS485_LEVEL_SHIFTER_IC "TXS0108E"
#define RS485_PART_NUMBER "MIKROE-4156"

// Pin definitions for ESP32 side (3.3V LDO always-on)
#define RS485_TX_PIN 16                 // ESP32 TX -> TXS0108E A1 -> RS485 DI
#define RS485_RX_PIN 17                 // ESP32 RX <- TXS0108E A2 <- RS485 RO
#define RS485_DE_RE_PIN 25              // Driver/Receiver Enable via TXS0108E A3
// Note: TXS0108E OE pin connected to VCC (always enabled)
// Note: No power enable pin needed - RS485 module always powered by 5V LDO

// RS485 Bus Configuration
#define RS485_BAUD_RATE 9600           // Standard Modbus RTU baud rate
#define RS485_DATA_BITS 8              // 8 data bits
#define RS485_STOP_BITS 1              // 1 stop bit
#define RS485_PARITY SERIAL_8N1        // No parity

// Hardware-managed LDO system - always-on operation
#define RS485_HAS_BUILTIN_ISOLATION false  // No ground isolation (common ground design)
#define RS485_CUSTOM_GROUND_ISOLATION false // Not required for LDO common ground system
#define RS485_GROUND_ISOLATION_METHOD "TXS0108E_LEVEL_SHIFTING_ONLY"

// Hardware-managed timing constants (LDO always-on system with TXS0108E)
// Timing values verified to work with direct RS485 testing
#define RS485_DE_ASSERT_DELAY_US 50    // Increased delay for reliable TXS0108E direction control
#define RS485_DE_DEASSERT_DELAY_US 50  // Increased delay for reliable TXS0108E direction control
#define RS485_RX_TIMEOUT_MS 1000       // Receive timeout
#define RS485_POWER_STABILIZE_MS 0     // No stabilization needed (always-on LDO)
#define RS485_DEFAULT_TIMEOUT_MS 3000  // Standard timeout for communication
#define RS485_POWER_ON_DELAY_MS 10     // Short delay for TXS0108E stabilization

// TXS0108E level shifter parameters
#define TXS0108E_PROPAGATION_DELAY_NS 10    // Propagation delay through TXS0108E
#define TXS0108E_MAX_DATA_RATE_MBPS 110     // Maximum data rate for TXS0108E
#define TXS0108E_ENABLE_TIME_US 1           // Time for TXS0108E to enable

// Bus termination and protection
#define RS485_TERMINATION_OHMS 120     // Bus termination resistance
#define RS485_ESD_PROTECTION_KV 15     // ESD protection level (±15kV)
#define RS485_ISOLATION_VOLTAGE_VRMS 0  // No isolation voltage (level shifter, not isolated)

// Hardware-managed LDO system specifications
// No complex ground isolation components needed
// AMS1117 LDO regulators provide stable always-on power

// Total system isolation specifications - LEVEL SHIFTING ONLY
#define TOTAL_SYSTEM_ISOLATION_KV 0.0    // No isolation with TXS0108E level shifter
#define GROUND_ISOLATION_METHOD_DESC "Common ground with TXS0108E level shifting"
#define SAFETY_CLASSIFICATION "Hardware_LDO_Managed_Level_Shifted"

// Power specifications - HARDWARE-MANAGED LDO
#define ESP32_DOMAIN_VOLTAGE_V 3.3     // ESP32 and control logic voltage (AMS1117-3.3)
#define FIELD_DOMAIN_VOLTAGE_V 5.0     // Field domain for RS485 and sensors (AMS1117-5.0)

// Safety and compliance - COMMON GROUND DESIGN
#define RS485_GALVANIC_ISOLATION false // No galvanic isolation (common ground)
#define RS485_SIGNAL_ISOLATION false   // Level shifting, not isolation
#define RS485_GROUND_LOOP_PROTECTION false // Not needed with common ground

/**
 * @brief Hardware feature flags - LDO-MANAGED SYSTEM
 */
#define RS485_HAS_OPTICAL_ISOLATION 0      // No optical isolation with TXS0108E
#define RS485_HAS_LEVEL_SHIFTING 1         // Level shifting via TXS0108E
#define RS485_HAS_ESD_PROTECTION 1         // ESD protection on RS485 module
#define RS485_HAS_SEPARATE_POWER_DOMAINS 0 // Common ground design (LDO-managed)
#define RS485_HAS_ALWAYS_ON_POWER 1        // Always-on LDO power supplies

/**
 * @brief Connection verification for LDO-managed system
 * These defines help verify correct hardware connections
 */
#define RS485_EXPECTED_MODULE "RS485 5 Click"
#define RS485_EXPECTED_LEVEL_SHIFTER "TXS0108E"
#define RS485_MIN_ISOLATION_VOLTAGE 0 // No isolation voltage required
#define RS485_EXPECTED_POWER_SUPPLY "AMS1117-5.0" // Expected 5V LDO regulator

#endif // WATERINGSYSTEM_HARDWARE_RS485CONFIG_H
