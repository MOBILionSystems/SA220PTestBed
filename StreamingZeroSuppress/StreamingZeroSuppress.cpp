///
/// Acqiris IVI-C Driver Example Program
///
/// Initializes the driver, reads a few Identity interface properties, and performs a
/// streaming acquisition.
///
/// For additional information on programming with IVI drivers in various IDEs, please see
/// http://www.ivifoundation.org/resources/
///
/// Requires a real instrument having CST and ZS1 options and an input signal on "Channel1".
///

#include "../include/LibTool.h"
using namespace LibTool::ZeroSuppress;
using LibTool::ToString;
#include "AqMD3.h"

#include <iomanip>
#include <iostream>
using std::cout;
using std::cerr;
using std::hex;
#include <vector>
using std::vector;
#include <stdexcept>
using std::runtime_error;
#include <chrono>
using std::chrono::minutes;
using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::system_clock;
#include <thread>
using std::this_thread::sleep_for;
#include<fstream>
#include <algorithm>


typedef std::vector<int32_t> FetchBuffer;

#define checkApiCall( f ) do { ViStatus s = f; testApiCall( s, #f ); } while( false )
void testApiCall(ViStatus status, char const* functionName);

// Get the sample at 'index' as 16-bit signed integer from the stream of 32-bit elements.
ViInt16 GetSample(LibTool::ArraySegment<int32_t> const& sampleBuffer, int64_t index);

//! Unpack the gates of a record described by descriptor 'recordDesc' into 'output'.
void UnpackRecord(RecordDescriptor const& recordDesc, LibTool::ArraySegment<int32_t> const& sampleBuffer, ProcessingParameters const& processingParams, std::ostream& output);

//! Fetch all elements available on module for stream streamName.
/*! The resulting array segment might be empty if no data are available on module.
     \param[in] session: session handle associated with the instrument to read from.
     \param[in] streamName: the stream identifier to read from.
     \param[in] maxElementsToFetch: the maximum number of elements to read.
     \param[in] buffer: buffer used by fetch for read operation.
     \return the array-segment delimiting actual valid data returned by the instrument. The segment might be empty if no data has been read.*/
LibTool::ArraySegment<int32_t> FetchAvailableElements(ViSession session, ViConstString streamName, ViInt64 maxElementsToFetch, FetchBuffer& buffer);

//! Perform a fetch of exact 'nbrElementsToFetch' elements into the given fetch "buffer".
/*! This is a wrapper for low-level function #AqMD3_StreamFetchDataInt32 which adds the following:
      - Return an array-segment to delimit the fetched elements in user-provided buffer.
      - Handle the case where the user request to fetch 0 elements from the stream by returning an empty array-segment. This might be useful when
        all the samples of a record are suppressed (i.e. nothing to read from sample stream).
      - Check the consistency of returned
    \param[in] session: session handle associated with the instrument to read from.
    \param[in] streamName: the stream identifier to read from.
    \param[in] nbrElementsToFetch: the number of elements to read.
    \param[in] buffer: buffer used by fetch for read operation.
    \return the array-segment delimiting actual valid data returned by the instrument. The segment might be empty if no data has been read.
    \throw #std::runtime_error when the buffer size is too small for the requested fetch, or the number fetched elements is different than requested.*/
LibTool::ArraySegment<int32_t> FetchElements(ViSession session, ViConstString streamName, ViInt64 nbrElementsToFetch, FetchBuffer& buffer);

//! Return the processing parameters for the given instrument model.
ProcessingParameters GetProcessingParametersForModel(std::string const& instrumentModel);

// name-space gathering all user-configurable parameters
namespace
{
    // Edit resource and options as needed. Resource is ignored if option has Simulate=true.
    // An input signal is necessary if the example is run in non simulated mode, otherwise
    // the acquisition will time out.
    ViChar resource[] = "PXI79::0::0::INSTR";
    ViChar options[] = "Simulate=false";

    // Acquisition configuration parameters
    ViReal64 const sampleRate = 2.0e9;
    ViReal64 const sampleInterval = 1.0 / sampleRate;
    ViInt64 const recordSize = 5120;
    ViInt32 const streamingMode = AQMD3_VAL_STREAMING_MODE_TRIGGERED;
    ViInt32 const acquisitionMode = AQMD3_VAL_ACQUISITION_MODE_NORMAL;
    ViInt32 const dataReductionMode = AQMD3_VAL_ACQUISITION_DATA_REDUCTION_MODE_ZERO_SUPPRESS;
    ViInt32 const nbrOfAverages = 3;

    // Channel configuration parameters
    ViConstString channel = "Channel1";
    ViReal64 const channelRange = 0.5;
    ViReal64 const channelOffset = 0.24;
    ViInt32 const coupling = AQMD3_VAL_VERTICAL_COUPLING_DC;

    // Channel ZeroSuppress configuration parameters
    ViInt32 const zsThreshold = -29706;
    ViInt32 const zsHysteresis = 100;
    ViInt32 const zsPreGateSamples = 0;
    ViInt32 const zsPostGateSamples = 0;

    // Trigger configuration
    ViConstString triggerSource = "External1";
    ViReal64 const triggerLevel = 0.5;
    ViInt32 const triggerSlope = AQMD3_VAL_TRIGGER_SLOPE_POSITIVE;

    // Readout parameters
    ViInt64 const nbrSamplesPerElement = (acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER) ? 1 : 2;
    ViConstString sampleStreamName = "StreamCh1";
    ViConstString markerStreamName = "MarkersCh1";

    // processing control parameters
    ViInt64 const maxRecordsToProcessAtOnce = 8;        // maximum number of records to process at once
    ViInt64 const nbrEstimatedGatesPerRecord = 4;       // estimation of the count of gates per record.
    ViInt64 const estimatedMarkerElementsPerRecord = LibTool::AlignUp(ViInt64(16)                       // trigger marker elements
        + nbrEstimatedGatesPerRecord * 4  // gate marker elements
        , ViInt64(16)                     // align to the next multiple of 16
    );

    ViInt64 const nbrMarkerElementsToFetch = maxRecordsToProcessAtOnce * estimatedMarkerElementsPerRecord;
    ViInt64 const nbrAcquisitionElements = maxRecordsToProcessAtOnce * recordSize / nbrSamplesPerElement;

    // Streaming session parameters
    seconds streamingDuration = seconds(5);

    /*wait-time before a new attempt of read operation.
      NOTE: Please tune according to your system input (trigger rate & number of peaks)*/
    auto const dataWaitTime = milliseconds(200);

    // Output file
    std::string const outputFileName("StreamingZeroSuppress.log");
}

int main()
{
    cout << "Triggered Streaming with ZeroSuppress\n\n";

    // Initialize the driver. See driver help topic "Initializing the IVI-C Driver" for additional information.
    ViSession session = VI_NULL;
    ViBoolean const idQuery = VI_FALSE;
    ViBoolean const reset = VI_FALSE;

    try
    {
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
        std::string const instrumentModel(str);
        checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_INFO_OPTIONS, sizeof(str), str));
        cout << "Instrument options: " << str << '\n';
        std::string const options(str);
        checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_FIRMWARE_REVISION, sizeof(str), str));
        cout << "Firmware revision:  " << str << '\n';
        checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_INFO_SERIAL_NUMBER_STRING, sizeof(str), str));
        cout << "Serial number:      " << str << '\n';
        cout << '\n';

        // Abort execution if instrument is still in simulated mode.
        ViBoolean simulate;
        checkApiCall(AqMD3_GetAttributeViBoolean(session, "", AQMD3_ATTR_SIMULATE, &simulate));
        if (simulate == VI_TRUE)
        {
            cout << "\nThe Streaming features are not supported in simulated mode.\n";
            cout << "Please update the resource string (resource[]) to match your configuration,";
            cout << " and update the init options string (options[]) to disable simulation.\n";

            AqMD3_close(session);

            return 1;
        }

        if (options.find("CST") == std::string::npos || options.find("ZS1") == std::string::npos)
        {
            cout << "The required CST & ZS1 module options are missing from the instrument.\n";

            AqMD3_close(session);

            return 1;
        }

        if ((acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER) && (options.find("AVG") == std::string::npos))
        {
            cout << "The required AVG module option is missing from the instrument.\n";

            AqMD3_close(session);

            return 1;
        }

        // Configure the acquisition in triggered mode with ZeroSuppress enabled.
        cout << "Configuring Acquisition\n";
        cout << "  Record size :        " << recordSize << '\n';
        cout << "  Streaming mode :     " << streamingMode << '\n';
        cout << "  SampleRate:          " << sampleRate << '\n';
        cout << "  Acquisition mode: " << acquisitionMode << '\n';
        cout << "  Data Reduction mode: " << dataReductionMode << '\n';
        if (acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER)
            checkApiCall(AqMD3_SetAttributeViInt32(session, "", AQMD3_ATTR_ACQUISITION_NUMBER_OF_AVERAGES, nbrOfAverages));
        checkApiCall(AqMD3_SetAttributeViInt32(session, "", AQMD3_ATTR_STREAMING_MODE, streamingMode));
        checkApiCall(AqMD3_SetAttributeViReal64(session, "", AQMD3_ATTR_SAMPLE_RATE, sampleRate));
        checkApiCall(AqMD3_SetAttributeViInt32(session, "", AQMD3_ATTR_ACQUISITION_DATA_REDUCTION_MODE, dataReductionMode));
        checkApiCall(AqMD3_SetAttributeViInt64(session, "", AQMD3_ATTR_RECORD_SIZE, recordSize));
        checkApiCall(AqMD3_SetAttributeViInt32(session, "", AQMD3_ATTR_ACQUISITION_MODE, acquisitionMode));

        // Configure the channels.
        cout << "Configuring " << channel << "\n";
        cout << "  Range:              " << channelRange << '\n';
        cout << "  Offset:             " << channelOffset << '\n';
        cout << "  Coupling:           " << (coupling ? "DC" : "AC") << '\n';
        checkApiCall(AqMD3_ConfigureChannel(session, channel, channelRange, channelOffset, coupling, VI_TRUE));

        // Configure ZeroSuppress
        cout << "Configuring ZeroSuppress\n";
        cout << "  Threshold:          " << zsThreshold << '\n';
        cout << "  Hysteresis:         " << zsHysteresis << '\n';
        cout << "  PreGate Samples:    " << zsPreGateSamples << '\n';
        cout << "  PostGate Samples:   " << zsPostGateSamples << '\n';
        checkApiCall(AqMD3_SetAttributeViInt32(session, channel, AQMD3_ATTR_CHANNEL_ZERO_SUPPRESS_HYSTERESIS, zsHysteresis));
        checkApiCall(AqMD3_SetAttributeViInt32(session, channel, AQMD3_ATTR_CHANNEL_ZERO_SUPPRESS_THRESHOLD, zsThreshold));
        checkApiCall(AqMD3_SetAttributeViInt32(session, channel, AQMD3_ATTR_CHANNEL_ZERO_SUPPRESS_PRE_GATE_SAMPLES, zsPreGateSamples));
        checkApiCall(AqMD3_SetAttributeViInt32(session, channel, AQMD3_ATTR_CHANNEL_ZERO_SUPPRESS_POST_GATE_SAMPLES, zsPostGateSamples));

        // Configure the trigger.
        cout << "Configuring Trigger\n";
        cout << "  ActiveSource:       " << triggerSource << '\n';
        cout << "  Level:              " << triggerLevel << "\n";
        cout << "  Slope:              " << (triggerSlope ? "Positive" : "Negative") << "\n";
        checkApiCall(AqMD3_SetAttributeViString(session, "", AQMD3_ATTR_ACTIVE_TRIGGER_SOURCE, triggerSource));
        checkApiCall(AqMD3_SetAttributeViReal64(session, triggerSource, AQMD3_ATTR_TRIGGER_LEVEL, triggerLevel));
        checkApiCall(AqMD3_SetAttributeViInt32(session, triggerSource, AQMD3_ATTR_TRIGGER_SLOPE, triggerSlope));

        // Calibrate the instrument.
        cout << "\nApply setup and run self-calibration\n";
        checkApiCall(AqMD3_ApplySetup(session));
        checkApiCall(AqMD3_GetAttributeViString(session, "", AQMD3_ATTR_INSTRUMENT_FIRMWARE_REVISION, sizeof(str), str));
        cout << "Firmware revision:  " << str << '\n';
        checkApiCall(AqMD3_SelfCalibrate(session));

        // Prepare readout buffers
        ViInt64 sampleStreamGrain = 0;
        ViInt64 markerStreamGrain = 0;
        checkApiCall(AqMD3_GetAttributeViInt64(session, sampleStreamName, AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES, &sampleStreamGrain));
        checkApiCall(AqMD3_GetAttributeViInt64(session, markerStreamName, AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES, &markerStreamGrain));

        ViInt64 const markerStreamGrainElements = markerStreamGrain / sizeof(ViInt32);
        ViInt64 const sampleStreamGrainElements = sampleStreamGrain / sizeof(ViInt32);
        ViInt64 const markerBufferSize = nbrMarkerElementsToFetch /*buffer size*/ + (markerStreamGrainElements - 1) /* alignment overhead */;
        ViInt64 const sampleBufferSize = nbrAcquisitionElements          /* useful buffer size */
            + nbrAcquisitionElements / 2      /* unfolding overhead (only required for SA248) */
            + (sampleStreamGrainElements - 1) /* alignment overhead */;

        FetchBuffer markerStreamBuffer((size_t)markerBufferSize);
        FetchBuffer sampleStreamBuffer((size_t)sampleBufferSize);

        // processing parameters
        ProcessingParameters const processingParams = GetProcessingParametersForModel(instrumentModel);

        // marker stream decoder
        MarkerStreamDecoder::Mode const markerDecoderMode = (dataReductionMode == AQMD3_VAL_ACQUISITION_DATA_REDUCTION_MODE_DISABLED)
            ? MarkerStreamDecoder::Mode::Normal
            : MarkerStreamDecoder::Mode::ZeroSuppress;
        MarkerStreamDecoder markerStreamDecoder(markerDecoderMode);

        // track the total number of fetched marker elements and sample elements.
        ViInt64 totalSampleElements = 0;
        ViInt64 totalMarkerElements = 0;

        // output file
        std::ofstream outputFile(outputFileName);
        outputFile << "model             : " << instrumentModel << "\n"
            << "record size       : " << recordSize << "\n"
            << "acquisition mode  : " << acquisitionMode << "\n";

        if (acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER)
            outputFile << "  nbr of averages : " << nbrOfAverages << "\n";

        outputFile << "data-reduction    : " << dataReductionMode << "\n";

        if (dataReductionMode == AQMD3_VAL_ACQUISITION_DATA_REDUCTION_MODE_ZERO_SUPPRESS)
            outputFile << "  threshold         : " << zsThreshold << "\n"
            << "  hysteresis        : " << zsHysteresis << "\n"
            << "  pre-gate samples  : " << zsPreGateSamples << "\n"
            << "  post-gate samples : " << zsPostGateSamples << "\n\n";

        LibTool::MarkerTag const expectedTag = (acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER) ? LibTool::MarkerTag::TriggerAverager : LibTool::MarkerTag::TriggerNormal;
        // Start the acquisition.
        cout << "\nInitiating acquisition\n";
        checkApiCall(AqMD3_InitiateAcquisition(session));
        cout << "Acquisition is running\n\n";

        // next record index to expect
        ViInt64 expectedRecordIndex = 0;

        auto endTime = system_clock::now() + streamingDuration;
        while (system_clock::now() < endTime)
        {
            // wait for markers to come
            for (; (system_clock::now() < endTime) && (markerStreamDecoder.GetAvailableRecordCount() == 0);)
            {
                // process all the available data without waiting again.
                LibTool::ArraySegment<int32_t> markerArraySegment = FetchAvailableElements(session, markerStreamName, nbrMarkerElementsToFetch, markerStreamBuffer);
                totalMarkerElements += markerArraySegment.Size();

                // If the fetch fails to read data, then wait before a new attempt.
                if (markerArraySegment.Size() == 0)
                {
                    std::cout << "waiting for data\n";
                    sleep_for(dataWaitTime);
                    continue;
                }

                // decode all fetched marker elements
                while (markerArraySegment.Size() > 0)
                    markerStreamDecoder.DecodeNextMarker(markerArraySegment);
            }

            ViInt64 const nbrRecordsToProcess = std::min(maxRecordsToProcessAtOnce, int64_t(markerStreamDecoder.GetAvailableRecordCount()));

            if (nbrRecordsToProcess == 0)
                continue;

            // Take out the "nbrRecordsToProcess" record descriptors to process from the queue.
            std::vector<RecordDescriptor> const recordDescriptorList = markerStreamDecoder.Take(int(nbrRecordsToProcess));

            // Compute the total count of samples associated with the record
            int64_t const storedSampleCount = (dataReductionMode == AQMD3_VAL_ACQUISITION_DATA_REDUCTION_MODE_ZERO_SUPPRESS)
                // In ZeroSuppress mode, the volume of samples corresponding to the "packed gates". It is computed according to record descriptors
                ? GetStoredSampleCountForRecords(recordDescriptorList, processingParams)
                // If data reduction is disabled, then the volume of samples corresponds to the configured record size (for each record).
                : nbrRecordsToProcess * recordSize
                ;

            int64_t const totalSampleElementCount = storedSampleCount / nbrSamplesPerElement;

            // fetch samples associated with all records at once
            LibTool::ArraySegment<int32_t> sampleArraySegment = FetchElements(session, sampleStreamName, totalSampleElementCount, sampleStreamBuffer);
            totalSampleElements += sampleArraySegment.Size();

            // iterate over record descriptors, check consistency of markers and save data into file.
            for (auto const& record : recordDescriptorList)
            {
                // Check that the record descriptor holds the expected record index and the expected tag
                if ((expectedRecordIndex & LibTool::TriggerMarker::RecordIndexMask) != record.GetTriggerMarker().recordIndex)
                    throw std::runtime_error("Unexpected record index: expected=" + ToString(expectedRecordIndex) + ", got " + ToString(record.GetTriggerMarker().recordIndex));

                if (expectedTag != record.GetTriggerMarker().tag)
                    throw std::runtime_error("Unexpected trigger tag, got " + ToString(int(record.GetTriggerMarker().tag)) + ", expected " + ToString(int(expectedTag)));

                // Rebuild the record and print related information into output file
                UnpackRecord(record, sampleArraySegment, processingParams, outputFile);

                // Remove the samples associated with the current record from the buffer.
                int64_t const nbrPackedRecordElements = (dataReductionMode == AQMD3_VAL_ACQUISITION_DATA_REDUCTION_MODE_ZERO_SUPPRESS)
                    ? record.GetStoredSampleCount(processingParams) / nbrSamplesPerElement
                    : recordSize / nbrSamplesPerElement;
                ;
                sampleArraySegment.PopFront(size_t(nbrPackedRecordElements));

                ++expectedRecordIndex;
            }
        }

        outputFile.close();
        ViInt64 const totalSampleData = totalSampleElements * sizeof(ViInt32);
        ViInt64 const totalMarkerData = totalMarkerElements * sizeof(ViInt32);
        cout << "\nTotal sample data read: " << (totalSampleData / (1024 * 1024)) << " MBytes.\n";
        cout << "Total marker data read: " << (totalMarkerData / (1024 * 1024)) << " MBytes.\n";
        cout << "Duration: " << (streamingDuration / seconds(1)) << " seconds.\n";
        ViInt64 const totalData = totalSampleData + totalMarkerData;
        cout << "Data rate: " << (totalData) / (1024 * 1024) / (streamingDuration / seconds(1)) << " MB/s.\n";

        // Stop the acquisition.
        cout << "\nStopping acquisition\n";
        checkApiCall(AqMD3_Abort(session));

        // Close the driver.
        checkApiCall(AqMD3_close(session));
        cout << "\nDriver closed\n";
        return 0;
    }
    catch (std::exception const& exc)
    {
        std::cerr << "Unexpected error: " << exc.what() << std::endl;

        if (session != VI_NULL)
            checkApiCall(AqMD3_close(session));
        cout << "\nDriver closed\n";

        return(1);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//
// definition of Helper functions
//

void testApiCall(ViStatus status, char const* functionName)
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

void UnpackRecord(
    RecordDescriptor const& recordDesc
    , LibTool::ArraySegment<int32_t> const& sampleBuffer
    , ProcessingParameters const& processingParams
    , std::ostream& output)
{
    LibTool::TriggerMarker const& trig = recordDesc.GetTriggerMarker();
    double const xTime = trig.GetInitialXTime(processingParams.timestampPeriod);
    double const xOffset = trig.GetInitialXOffset(sampleInterval);
    output << "# record index      : " << std::dec << trig.recordIndex << '\n';
    output << " * Time of Sample #0: " << std::setprecision(12) << xTime << '\n';
    output << " * Time of Trigger  : " << std::setprecision(12) << xTime + xOffset << '\n';

    output << std::dec;

    // track the actual size of record (as invalid pre-record samples might be stored by the firmware)
    int64_t actualRecordSize = recordSize;

    // The offset at which starts storage of gate samples (including the leading suppressed samples)
    int64_t nextGateOffsetInMemory = 0;
    for (LibTool::ZeroSuppress::GateMarker const& gate : recordDesc.GetGateList())
    {
        int64_t const gateStartIndex = gate.GetStartMarker().GetStartSampleIndex(processingParams);
        int64_t const gateStopIndex = gate.GetStopMarker().GetStopSampleIndex(processingParams);

        int64_t leadingSamplesToSkip = gate.GetStartMarker().GetSuppressedSampleCount(processingParams);
        if (gateStartIndex < processingParams.preGateSamples)
        {
            // pre-gate samples acquired before the very first sample of the record are not correct. They must be removed from the record.
            int32_t const preRecordSamples = processingParams.preGateSamples - int32_t(gateStartIndex);
            // samples are stored by blocks of #processingBlockSamples samples.
            int32_t const invalidStoredSamples = LibTool::AlignUp(preRecordSamples, processingParams.processingBlockSamples);
            actualRecordSize = std::max(int64_t(0), recordSize - invalidStoredSamples);
            leadingSamplesToSkip = invalidStoredSamples;
        }

        // data start/stop indices (data represent gate, pre and post gate samples)
        int64_t const dataStartIndex = std::max(int64_t(0), gateStartIndex - processingParams.preGateSamples);
        int64_t const dataStopIndex = std::min(gateStopIndex + processingParams.postGateSamples, actualRecordSize);
        int64_t const dataStartIndexInMemory = nextGateOffsetInMemory + leadingSamplesToSkip;

        output << " - Gate samples=#" << gateStartIndex              // is the index of the first sample of the gate (above the configured threshold)
            << ".." << gateStopIndex - 1                           // is the index of the last sample of the gate (above the configured threshold-hysteresis)
            << ", pre-gate=#" << gateStartIndex - dataStartIndex // actual pre-gate samples
            << ", post-gate=#" << dataStopIndex - gateStopIndex  // actual post-gate samples (might be negative)
            << ", data samples(#" << dataStartIndex              // is the index of the first valid pre-gate sample
            << ".." << dataStopIndex - 1                           // is the index of the last valid sample of the gate or post-gate.
            << ")=[";                                            // array of valid data (with pre-gate, gate and post-gate samples)

        if (acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER)
        {
            // samples are 32-bit in averager acquisition mode
            int32_t const* const sampleArray = sampleBuffer.GetData();
            for (int64_t sampleIndexInMemory = dataStartIndexInMemory, samplePosition = dataStartIndex; samplePosition < dataStopIndex; ++samplePosition, ++sampleIndexInMemory)
            {
                output << sampleArray[sampleIndexInMemory];

                if (samplePosition < dataStopIndex - 1)
                    output << ", ";
            }
        }
        else
        {
            // samples are 16-bit in normal acquisition mode
            int16_t const* const sampleArray = reinterpret_cast<int16_t const* const>(sampleBuffer.GetData());
            for (int64_t sampleIndexInMemory = dataStartIndexInMemory, samplePosition = dataStartIndex; samplePosition < dataStopIndex; ++samplePosition, ++sampleIndexInMemory)
            {
                output << sampleArray[sampleIndexInMemory];

                if (samplePosition < dataStopIndex - 1)
                    output << ", ";
            }
        }

        output << "]\n";
        output.flush();
        nextGateOffsetInMemory += gate.GetStoredSampleCount(processingParams, recordDesc.GetRecordStopMarker());
    }

    output << "actual record size: " << actualRecordSize << "\n\n";
}

LibTool::ArraySegment<int32_t> FetchAvailableElements(ViSession session, ViConstString streamName, ViInt64 nbrElementsToFetch, FetchBuffer& buffer)
{
    int32_t* bufferData = buffer.data();
    ViInt64 const bufferSize = buffer.size();

    if (bufferSize < nbrElementsToFetch)
        throw std::invalid_argument("Buffer size is smaller than the requested elements to fetch");

    ViInt64 firstValidElement = 0;
    ViInt64 actualElements = 0;
    ViInt64 remainingElements = 0;

    // Try to fetch the requested volume of elements.
    checkApiCall(AqMD3_StreamFetchDataInt32(session, streamName, nbrElementsToFetch, bufferSize, (ViInt32*)bufferData, &remainingElements, &actualElements, &firstValidElement));

    if ((actualElements == 0) && (remainingElements > 0))
    {
        /* Fetch failed to read data because the number of available elements is smaller than the requested volume.*/

        // Check that the number of available elements is smaller than requested
        if (nbrElementsToFetch <= remainingElements)
            throw std::logic_error("First fetch failed to read " + ToString(nbrElementsToFetch) + " elements when it reports " + ToString(remainingElements) + " available elements.");

        // Read available elements
        checkApiCall(AqMD3_StreamFetchDataInt32(session, streamName, remainingElements, bufferSize, (ViInt32*)bufferData, &remainingElements, &actualElements, &firstValidElement));
    }

    if (actualElements > 0)
    {
        std::cout << "\nFetched " << actualElements << " elements from " << streamName << " stream. Remaining elements: " << remainingElements << "\n";
    }

    // this buffer might be empty if fetch failed to read actual data.
    return LibTool::ArraySegment<int32_t>(buffer, (size_t)firstValidElement, (size_t)actualElements);
}

LibTool::ArraySegment<int32_t> FetchElements(ViSession session, ViConstString streamName, ViInt64 nbrElementsToFetch, FetchBuffer& buffer)
{
    // Handle the special case there is no need to fetch elements (this might happen when all samples are suppressed) by returning an empty array segment
    if (nbrElementsToFetch == 0)
        return LibTool::ArraySegment<int32_t>(buffer, 0, 0);

    int32_t* bufferData = buffer.data();
    ViInt64 const bufferSize = buffer.size();

    if (bufferSize < nbrElementsToFetch)
        throw std::invalid_argument("Buffer size is smaller than the requested elements to fetch");

    ViInt64 firstElement = 0;
    ViInt64 actualElements = 0;
    ViInt64 remainingElements = 0;

    // Try to fetch the requested volume of elements.
    checkApiCall(AqMD3_StreamFetchDataInt32(session, streamName, nbrElementsToFetch, bufferSize, (ViInt32*)bufferData, &remainingElements, &actualElements, &firstElement));

    // if fetched data volume is equal to the requested, there is no thing else to do.
    if (nbrElementsToFetch != actualElements)
    {
        throw std::runtime_error("Number of fetched elements is different than requested. Requested=" + ToString(nbrElementsToFetch) + " , fetched=" + ToString(actualElements) + ".");
    }
    else
    {
        std::cout << "\nFetched " << actualElements << " elements from " << streamName << " stream. Remaining elements: " << remainingElements << "\n";
        return LibTool::ArraySegment<int32_t>(buffer, size_t(firstElement), size_t(actualElements));
    }
}

ProcessingParameters GetProcessingParametersForModel(std::string const& model)
{
    if (acquisitionMode == AQMD3_VAL_ACQUISITION_MODE_AVERAGER)
    {
        if (model == "SA220P" || model == "SA220E")
            return ProcessingParameters(16, 16, 500e-12, zsPreGateSamples, zsPostGateSamples);
        else
            throw std::invalid_argument("Real-Time Binning not supported on your instrument: " + model);
    }
    else
    {
        if (model == "SA220P" || model == "SA220E")
            return ProcessingParameters(32, 8, 500e-12, zsPreGateSamples, zsPostGateSamples);
        else if (model == "SA230P" || model == "SA230E")
            return ProcessingParameters(32, 16, 250e-12, zsPreGateSamples, zsPostGateSamples);
        else if (model == "SA240P" || model == "SA240E")
            return ProcessingParameters(32, 16, 250e-12, zsPreGateSamples, zsPostGateSamples);
        else if (model == "SA248P" || model == "SA248E")
            return ProcessingParameters(64, 32, 125e-12, zsPreGateSamples, zsPostGateSamples);
        else
            throw std::invalid_argument("Cannot deduce the size of processing block for your instrument: " + model);
    }
}
