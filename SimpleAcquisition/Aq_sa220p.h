#pragma once
#include "AqMD3.h"
#include <iostream>
using std::cout;
using std::cerr;
using std::hex;
#include <vector>
using std::vector;
#include <stdexcept>
using std::runtime_error;

/// @brief The default value for the digitizer sampling rate (samples / second).
constexpr ViReal64 kDefaultSampleRate = 2.0e9;
/// @brief The default value for the baseline stabilization pulse threshold.
constexpr ViInt32 kDefaultPulseThreshold = 200;
/// @brief The default value for the baseline stabilization digital offset.
constexpr ViInt32 kDefaultDigitalOffset = -31456;
/// @brief The default value for the digitizer's recordsize (samples / trigger).
constexpr ViInt32 kDefaultRecordSize = 320000;
/// @brief The default value for the digitizer's fullscale range (volts).
constexpr ViReal64 kDefaultRange = 0.5;
/// @brief The default value for the digitizer's range offset (volts).
constexpr ViReal64 kDefaultOffset = 0.24;
/// @brief The default value for the threshold value used in zero-suppression.
constexpr ViInt32 kDefaultZSThreshold = -29706;
/// @brief The default value used as the zero value used in zero-suppression.
constexpr ViInt32 kDefaultZeroValue = -31456;
/// @brief The default value used as the hysteresis value used in zero-suppresion.
constexpr ViInt32 kDefaultZSHysteresis = 100;
/// @brief The default value used as the pre gate setting in zero-suppression.
constexpr ViInt32 kDefaultZSPreGateSamples = 0;
/// @brief The default value used as the post gate setting in zero-suppression.
constexpr ViInt32 kDefaultZSPostGateSamples = 0;
/// @brief The default value used as the trigger level (volts).
constexpr ViReal64 kDefaultTriggerLevel = 2.5;
/// @brief The default value used as the minimum data point to record (nanoseconds).
constexpr ViInt32 kDefaultMinNanoseconds = 15000;
/// @brief The default value used as the self-trigger frequency.
constexpr ViReal64 kDefaultSelfTriggerFrequency = 10000.0;
/// @brief The default value used as the self-trigger duty cycle.
constexpr ViReal64 kDefaultSelfTriggerDutyCycle = 10.0;



class SA220P
{
public:
	SA220P();
	~SA220P();

	void init();
	void configure();
	void calibrate();
	void acquire();
	void close();

private:
	ViSession session;
	void testApiCall(ViStatus status, char const* functionName);
	// Edit resource and options as needed. Resource is ignored if option has Simulate=true.
// An input signal is necessary if the example is run in non simulated mode, otherwise
// the acquisition will time out.
	std::string _resource = "PXI79::0::0::INSTR";
	std::string _options = "Simulate=false";


	ViInt64 recordSize = 5120;
	ViInt64 numRecords = 1;

	ViConstString trigger_source = "External1";
	ViInt32 trigger_slope = AQMD3_VAL_TRIGGER_SLOPE_POSITIVE;
	ViReal64 trigger_level = 0.5; /// @brief The default value used as the trigger level (volts).

	  /// @brief The pulse polarity used for baseline correction.
	/// @brief Indicates whether or not baseline correction should be activated.
	ViInt32 baseline_stabilize_enable = 1;
	ViInt32 pulse_polarity = AQMD3_VAL_BASELINE_CORRECTION_PULSE_POLARITY_POSITIVE;
	/// @brief Baseline pulse detection threshold.
	ViInt32 pulse_threshold = 200;
	/// @brief Digital offset applied after baseline correction.
	ViInt32 digital_offset = -31456;
	/// @brief The baseline correction mode to employ.
	ViInt32 baseline_mode = AQMD3_VAL_BASELINE_CORRECTION_MODE_BETWEEN_ACQUISITIONS;
};


