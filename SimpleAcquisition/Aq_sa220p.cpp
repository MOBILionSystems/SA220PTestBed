#include "Aq_sa220p.h"
#define checkApiCall( f ) do { ViStatus s = f; testApiCall( s, #f ); } while( false )

SA220P::SA220P()
{
}

SA220P::~SA220P()
{
}

void SA220P::init()
{
    cout << "SimpleAcquisition\n\n";

    // Initialize the driver. See driver help topic "Initializing the IVI-C Driver" for additional information.
    ViBoolean const idQuery = VI_FALSE;
    ViBoolean const reset = VI_FALSE;
    char* resource = const_cast<char*>(_resource.c_str());
    char* options = const_cast<char*>(_options.c_str());
    checkApiCall(AqMD3_InitWithOptions(resource, idQuery, reset, options, &session));

    cout << "Driver initialized \n";

    // Read and output a few attributes.
    ViChar str[128];
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_SPECIFIC_DRIVER_PREFIX, sizeof(str), str));
    cout << "Driver prefix:      " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_SPECIFIC_DRIVER_REVISION, sizeof(str), str));
    cout << "Driver revision:    " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_SPECIFIC_DRIVER_VENDOR, sizeof(str), str));
    cout << "Driver vendor:      " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_SPECIFIC_DRIVER_DESCRIPTION, sizeof(str), str));
    cout << "Driver description: " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_MODEL, sizeof(str), str));
    cout << "Instrument model:   " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_INFO_OPTIONS, sizeof(str), str));
    cout << "Instrument options: " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_FIRMWARE_REVISION, sizeof(str), str));
    cout << "Firmware revision:  " << str << '\n';
    checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_INFO_SERIAL_NUMBER_STRING, sizeof(str), str));
    cout << "Serial number:      " << str << '\n';

    ViBoolean simulate;
    checkApiCall(AqMD3_GetAttributeViBoolean(session, "", AQMD3_ATTR_SIMULATE, &simulate));
    cout << "\nSimulate:           " << (simulate ? "True" : "False") << '\n';
}

void SA220P::configure()
{
    // Configure the acquisition.
    ViReal64 const range = 0.5;
    ViReal64 const offset = 0.24;
    ViInt32 const coupling = AQMD3_VAL_VERTICAL_COUPLING_DC;
    cout << "\nConfiguring acquisition\n";
    cout << "Range:              " << range << '\n';
    cout << "Offset:             " << offset << '\n';
    cout << "Coupling:           " << (coupling ? "DC" : "AC") << '\n';
    checkApiCall(AqMD3_ConfigureChannel(session, "Channel1", range, offset, coupling, VI_TRUE));
    cout << "Number of records:  " << numRecords << '\n';
    cout << "Record size:        " << recordSize << '\n';
    checkApiCall(AqMD3_SetAttributeViInt64(session, "", AQMD3_ATTR_NUM_RECORDS_TO_ACQUIRE, numRecords));
    checkApiCall(AqMD3_SetAttributeViInt64(session, "", AQMD3_ATTR_RECORD_SIZE, recordSize));

    if (baseline_stabilize_enable != 0) {
        cout << "Setting up baseline correction" << std::endl;
        checkApiCall(AqMD3_ChannelBaselineCorrectionConfigure(session, "Channel1", baseline_mode, pulse_threshold,
            pulse_polarity, digital_offset));
        checkApiCall(AqMD3_ChannelBaselineCorrectionConfigure(session, "Channel2", baseline_mode, pulse_threshold,
            pulse_polarity, digital_offset));
    }


    // Configure the trigger.
    cout << "\nConfiguring trigger\n";
    checkApiCall(AqMD3_SetAttributeViString(session, "", AQMD3_ATTR_ACTIVE_TRIGGER_SOURCE, trigger_source));
    checkApiCall(AqMD3_SetAttributeViReal64(session, trigger_source, AQMD3_ATTR_TRIGGER_LEVEL, trigger_level));
    checkApiCall(AqMD3_SetAttributeViInt32(session, trigger_source, AQMD3_ATTR_TRIGGER_SLOPE, trigger_slope));
}

void SA220P::calibrate()
{
    // Calibrate the instrument.
    cout << "\nPerforming self-calibration\n";
    checkApiCall(AqMD3_SelfCalibrate(session));
}

void SA220P::acquire()
{
    ViReal64 const range = 0.5;
    // Perform the acquisition.
    ViInt32 const timeoutInMs = 1000;
    cout << "\nPerforming acquisition\n";
    checkApiCall(AqMD3_InitiateAcquisition(session));
    //checkApiCall(AqMD3_SendSoftwareTrigger(session));
    checkApiCall(AqMD3_WaitForAcquisitionComplete(session, timeoutInMs));

    cout << "Acquisition completed\n";

    // Fetch the acquired data in array.
    ViInt64 arraySize = 0;
    checkApiCall(AqMD3_QueryMinWaveformMemory(session, 16, numRecords, 0, recordSize, &arraySize));

    vector<ViInt16> dataArray(static_cast<size_t>(arraySize));
    ViInt64 actualPoints, firstValidPoint;
    ViReal64 initialXOffset[5120], initialXTimeSeconds[5120], initialXTimeFraction[5120];
    ViReal64 xIncrement = 0.0, scaleFactor = 0, scaleOffset = 0.0;
    checkApiCall(AqMD3_FetchWaveformInt16(session, "Channel1", arraySize, &dataArray[0],
        &actualPoints, &firstValidPoint, initialXOffset, initialXTimeSeconds, initialXTimeFraction,
        &xIncrement, &scaleFactor, &scaleOffset));

    cout << "actualPoints: " << actualPoints << std::endl;
    // Convert data to Volts.
    cout << "\nProcessing data\n";
    for (ViInt64 currentPoint = 0; currentPoint < actualPoints; ++currentPoint)
    {
        cout << currentPoint << "current data: " << dataArray[static_cast<size_t>(firstValidPoint + currentPoint)] << std::endl;
        ViReal64 valueInVolts = dataArray[static_cast<size_t>(firstValidPoint + currentPoint)] * scaleFactor + scaleOffset;
        //cout << currentPoint << "current voltage: " << dataArray[static_cast<size_t>(firstValidPoint + currentPoint)] * (1.0 / 32768) * (range / 2) * 1000 << " mv" << std::endl;
    }

    cout << "Processing completed\n";
}

void SA220P::close()
{
    // Close the driver.
    checkApiCall(AqMD3_close(session));
    cout << "\nDriver closed\n";
}

// Utility function to check status error during driver API call.
void SA220P::testApiCall(ViStatus status, char const* functionName)
{
    ViInt32 ErrorCode;
    ViChar ErrorMessage[512];

    if (status > 0) // Warning occurred.
    {
        AqMD3_GetError(VI_NULL, &ErrorCode, sizeof(ErrorMessage), ErrorMessage);
        cerr << "** Warning during " << functionName << ": 0x" << hex << ErrorCode << ", " << ErrorMessage << '\n';

    }
    else if (status < 0) // Error occurred.
    {
        AqMD3_GetError(VI_NULL, &ErrorCode, sizeof(ErrorMessage), ErrorMessage);
        cerr << "** ERROR during " << functionName << ": 0x" << hex << ErrorCode << ", " << ErrorMessage << '\n';
        throw runtime_error(ErrorMessage);
    }
}
