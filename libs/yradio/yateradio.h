/**
 * yateradio.h
 * Radio library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
 * Copyright (C) 2015 LEGBA Inc
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __YATERADIO_H
#define __YATERADIO_H

#include <yateclass.h>
#include <yatexml.h>

#ifdef _WINDOWS

#ifdef LIBYRADIO_EXPORTS
#define YRADIO_API __declspec(dllexport)
#else
#ifndef LIBYRADIO_STATIC
#define YRADIO_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YRADIO_API
#define YRADIO_API
#endif


namespace TelEngine {

class GSML3Codec;                        // GSM Layer codec
class RadioCapability;                   // Radio device capabilities
class RadioInterface;                    // Generic radio interface

class YRADIO_API GSML3Codec
{
    YNOCOPY(GSML3Codec);
public:
    /**
     * Codec flags
     */
    enum Flags {
	XmlDumpMsg = 0x01,
	XmlDumpIEs = 0x02,
	MSCoder    = 0x04,
    };

    /**
     * Codec return status
     */
    enum Status {
	NoError = 0,
	MsgTooShort,
	UnknownProto,
	ParserErr,
	MissingParam,
	IncorrectOptionalIE,
	IncorrectMandatoryIE,
	MissingMandatoryIE,
	UnknownMsgType,
    };

    /**
     * Protocol discriminator according to ETSI TS 124 007 V11.0.0, section 11.2.3.1.1
     */
    enum Protocol {
	GCC       = 0x00, // Group Call Control
	BCC       = 0x01, // Broadcast Call Control
	EPS_SM    = 0x02, // EPS Session Management
	CC        = 0x03, // Call Control; Call Related SS messages
	GTTP      = 0x04, // GPRS Transparent Transport Protocol (GTTP)
	MM        = 0x05, // Mobility Management
	RRM       = 0x06, // Radio Resources Management
	EPS_MM    = 0x07, // EPS Mobility Management
	GPRS_MM   = 0x08, // GPRS Mobility Management
	SMS       = 0x09, // SMS
	GPRS_SM   = 0x0a, // GPRS Session Management
	SS        = 0x0b, // Non Call Related SS messages
	LCS       = 0x0c, // Location services
	Extension = 0x0e, // reserved for extension of the PD to one octet length
	Test      = 0x0f, // used by tests procedures described in 3GPP TS 44.014, 3GPP TS 34.109 and 3GPP TS 36.509
	Unknown   = 0xff,
    };

    /**
     * IE types
     */
    enum Type {
	NoType = 0,
	T,
	V,
	TV,
	LV,
	TLV,
	LVE,
	TLVE,
    };

    /**
     * Type of XML data to generate
     */
    enum XmlType {
	Skip,
	XmlElem,
	XmlRoot,
    };

    /**
     * EPS Security Headers
     */
    enum EPSSecurityHeader {
	PlainNAS                           = 0x00,
	IntegrityProtect                   = 0x01,
	IntegrityProtectCiphered           = 0x02,
	IntegrityProtectNewEPSCtxt         = 0x03,
	IntegrityProtectCipheredNewEPSCtxt = 0x04,
	ServiceRequestHeader               = 0xa0,
    };

    /**
     * Constructor
     */
    GSML3Codec(DebugEnabler* dbg = 0);

    /**
     * Decode layer 3 message payload
     * @param in Input buffer containing the data to be decoded
     * @param len Length of input buffer
     * @param out XmlElement into which the decoded data is returned
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int decode(const uint8_t* in, unsigned int len, XmlElement*& out, const NamedList& params = NamedList::empty());

    /**
     * Encode a layer 3 message
     * @param in Layer 3 message in XML form
     * @param out Output buffer into which to put encoded data
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int encode(const XmlElement* in, DataBlock& out, const NamedList& params = NamedList::empty());

    /**
     * Decode layer 3 message from an existing XML
     * @param xml XML which contains layer 3 messages to decode and into which the decoded XML will be put
     * @param params Decoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int decode(XmlElement* xml, const NamedList& params = NamedList::empty());

    /**
     * Encode a layer 3 message from an existing XML
     * @param xml XML which contains a layer 3 message in XML form. The message will be replaced with its encoded buffer
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int encode(XmlElement* xml, const NamedList& params = NamedList::empty());

    /**
     * Set data used in debug
     * @param enabler The DebugEnabler to use (0 to to use the engine)
     * @param ptr Pointer to print, 0 to use the codec pointer
     */
    void setCodecDebug(DebugEnabler* enabler = 0, void* ptr = 0);

    /**
     * Retrieve codec flags
     * @return Codec flags
     */
    inline uint8_t flags() const
	{ return m_flags; }

    /**
     * Set codec flags
     * @param flgs Flags to set
     * @param reset Reset flags before setting these ones
     */
    inline void setFlags(uint8_t flgs, bool reset = false)
    {
	if (reset)
	    resetFlags();
	m_flags |= flgs;
    }

    /**
     * Reset codec flags
     * @param flgs Flags to reset. If 0, all flags are reset
     */
    inline void resetFlags(uint8_t flgs = 0)
    {
	if (flgs)
	    m_flags &= ~flgs;
	else
	    m_flags = 0;
    }

    /**
     * Activate printing of debug messages
     * @param on True to activate, false to disable
     */
    inline void setPrintDbg(bool on = false)
	{ m_printDbg = on; }

    /**
     * Get printing of debug messages flag
     * @return True if debugging is activated, false otherwise
     */
    inline bool printDbg() const
	{ return m_printDbg; }

    /**
     * Get DebugEnabler used by this codec
     * @return DebugEnabler used by the codec
     */
    inline DebugEnabler* dbg() const
	{ return m_dbg; }

    /**
     * Retrieve the codec pointer used for debug messages
     * @return Codec pointer used for debug messages
     */
    inline void* ptr() const
	{ return m_ptr; }

    /**
     * Decode GSM 7bit buffer
     * @param buf Input buffer
     * @param len Input buffer length
     * @param text Destination text
     * @param heptets Maximum number of heptets in buffer
     */
    static void decodeGSM7Bit(unsigned char* buf, unsigned int len, String& text,
	unsigned int heptets = (unsigned int)-1);

    /**
     * Encode GSM 7bit buffer
     * @param text Input text
     * @param buf Destination buffer
     * @return True if all characters were encoded correctly
     */
    static bool encodeGSM7Bit(const String& text, DataBlock& buf);

    /**
     * IE types dictionary
     */
    static const TokenDict s_typeDict[];

    /**
     * L3 Protocols dictionary
     */
    static const TokenDict s_protoDict[];

    /**
     * EPS Security Headers dictionary
     */
    static const TokenDict s_securityHeaders[];

    /**
     * Errors dictionary
     */
    static const TokenDict s_errorsDict[];

    /**
     * Mobility Management reject causes dictionary
     */
    static const TokenDict s_mmRejectCause[];

    /**
     * GPRS Mobility Management reject causes dictionary
     */
    static const TokenDict s_gmmRejectCause[];

private:

    unsigned int decodeXml(XmlElement* xml, const NamedList& params, const String& pduTag);
    unsigned int encodeXml(XmlElement* xml, const NamedList& params, const String& pduTag);
    void printDbg(int dbgLevel, const uint8_t* in, unsigned int len, XmlElement* xml, bool encode = false);

    uint8_t m_flags;                 // Codec flags
    // data used for debugging messages
    DebugEnabler* m_dbg;
    void* m_ptr;
    // activate debug
    bool m_printDbg;
};


/**
 * @short Radio device capabilities
 * Radio capability object describes the parameter ranges of the radio handware.
 */
class YRADIO_API RadioCapability
{
public:
    /**
     * Constructor
     */
    RadioCapability();

    // Application-level parameters
    // Hardware
    unsigned maxPorts;                   // the maximum allowed ports
    unsigned currPorts;                  // the currently available ports
    // Tuning
    uint64_t maxTuneFreq;                // maximum allowed tuning frequency, Hz
    uint64_t minTuneFreq;                // minimum allowed tuning frequency, Hz
    // Output power
    float maxOutputPower;                // maximum allowed output power, dBm
    float minOutputPower;                // minimum allowed output power, dBm
    // Input gain (saturation point)
    float maxInputSaturation;            // maximum selectable saturation point, dBm
    float minInputSaturation;            // minimum selectable saturation point, dBm
    // Sample rate
    unsigned maxSampleRate;              // max in Hz
    unsigned minSampleRate;              // min in Hz
    // Anti-alias filter bandwidth
    unsigned maxFilterBandwidth;         // max in Hz
    unsigned minFilterBandwidth;         // min in Hz
    // Calibration parameters
    // Tx Gain Control
    int txGain1MaxVal;                   // max allowed value
    int txGain1MinVal;                   // min allowed value
    float txGain1StepSize;               // control step size in dB
    int txGain2MaxVal;                   // max allowed value
    int txGain2MinVal;                   // min allowed value
    float txGain2StepSize;               // control step size in dB
    // Rx Gain Control
    int rxGain1MaxVal;                   // max allowed value
    int rxGain1MinVal;                   // min allowed value
    float rxGain1StepSize;               // control step size in dB
    int rxGain2MaxVal;                   // max allowed value
    int rxGain2MinVal;                   // min allowed value
    float rxGain2StepSize;               // control step size in dB
    // Freq Cal Control
    unsigned freqCalControlMaxVal;       // maximum value of frequency control parameter
    unsigned freqCalControlMinVal;       // minimum value of frequency control parameter
    float freqCalControlStepSize;        // avg step size in Hz
    // IQ Offset Control
    unsigned iqOffsetMaxVal;             // max allowed value
    unsigned iqOffsetMinVal;             // min allowed value
    float iqOffsetStepSize;              // control step size as fraction of full scale
    // IQ Gain Balance Control
    unsigned iqBalanceMaxVal;            // max allowed value
    unsigned iqBalanceMinVal;            // min allowed value
    float iqBalanceStepSize;             // control step size in dB of I/Q
    // IQ Delay Control
    unsigned iqDelayMaxVal;              // max allowed value
    unsigned iqDelayMinVal;              // min allowed value
    float iqDelayStepSize;               // control step size in fractions of a sample
    // Phase control
    unsigned phaseCalMaxVal;             // max allowed value
    unsigned phaseCalMinVal;             // min allowed value
    float phaseCalStepSize;              // control step size in radians
};


/**
 * Keeps a buffer pointer with offset and valid samples
 * @short A buffer description
 */
class RadioBufDesc
{
public:
    /**
     * Constructor
     */
    inline RadioBufDesc()
	: samples(0), offs(0), valid(0)
	{}

    /**
     * Reset the buffer
     * @param value Offset and valid samples value
     */
    inline void reset(unsigned int value = 0)
	{ offs = valid = value; }

    /**
     * Reset the buffer
     * @param offset New offset
     * @param validS New valid samples value
     */
    inline void reset(unsigned int offset, unsigned int validS) {
	    offs = offset;
	    valid = validS;
	}

    /**
     * Check if the buffer is valid
     * @param minSamples Required minimum number of valid samples
     * @return True if valid, false otherwise
     */
    inline bool validSamples(unsigned int minSamples) const
	{ return !minSamples || minSamples >= offs || minSamples >= valid; }

    float* samples;                      // Current read buffer
    unsigned int offs;                   // Current buffer offset (in sample periods)
    unsigned int valid;                  // The number of valid samples in buffer
};


/**
 * Keeps buffers used by RadioInterface::read()
 * @short RadioInterface read buffers
 */
class RadioReadBufs : public GenObject
{
public:
    /**
     * Constructor
     * @param len Single buffer length (in sample periods)
     * @param validThres Optional threshold for valid samples. Used when read timestamp
     *  data is in the future and a portion of the buffer need to be reset.
     *  If valid samples are below threshold data won't be set or copied
     */
    inline RadioReadBufs(unsigned int len = 0, unsigned int validThres = 0)
	: m_bufSamples(len), m_validMin(validThres)
	{}

    /**
     * Reset buffers
     * @param len Single buffer length (in sample periods)
     * @param validThres Optional threshold for valid samples
     */
    inline void reset(unsigned int len, unsigned int validThres) {
	    m_bufSamples = len;
	    m_validMin = validThres;
	    crt.reset();
	    aux.reset();
	    extra.reset();
	}

    /**
     * Retrieve the length of a single buffer
     * @return Buffer length (in sample periods)
     */
    inline unsigned int bufSamples() const
	{ return m_bufSamples; }

    /**
     * Check if a given buffer is full (offset is at least buffer length)
     * @param buf Buffer to check
     * @return True if the given buffer is full, false otherwise
     */
    inline bool full(RadioBufDesc& buf) const
	{ return buf.offs >= m_bufSamples; }

    /**
     * Check if a given is valid (have enough valid samples)
     * @param buf Buffer to check
     * @return True if the given buffer is valid, false otherwise
     */
    inline bool valid(RadioBufDesc& buf) const
	{ return buf.validSamples(m_validMin); }

    /**
     * Dump data for debug purposes
     * @param buf Destination buffer
     * @return Destination buffer reference
     */
    String& dump(String& buf);

    RadioBufDesc crt;
    RadioBufDesc aux;
    RadioBufDesc extra;

protected:
    unsigned int m_bufSamples;           // Buffers length in sample periods
    unsigned int m_validMin;             // Valid samples threshold
};


/**
 * @short Generic radio interface
 *
 * @note Some parameters are quantized by the radio hardware. If the caller requests a parameter
 * value that cannot be matched exactly, the setting method will set the parameter to the
 * best avaiable match and return "NotExact". For such parameters, there is a corresponding
 * readback method to get the actual value used.
 *
 * @note If a method does not include a radio port number, then that method applies to all connected ports.
 *
 * @note The interface may control multiple radios, with each one appearing as a port. However, in this case,
 *  - all radios are to be synched on the sample clock
 *  - all radios are to be of the same hardware type
 *
 * @note If the performance of the radio hardware changes, the API indicates this with the RFHardwareChange
 * flag. If this flag appears in a result code, the application should:
 *  - read a new RadioCapabilties object
 *  - revisit all of the application-level parameter settings on the radio
 *  - check the status() method on each port to identify single-port errors
 */
class YRADIO_API RadioInterface : public RefObject, public DebugEnabler
{
    YCLASS(RadioInterface,RefObject);
public:
    /** Error code bit positions in the error code mask. */
    enum ErrorCode {
	NoError = 0,
	Failure = (1 << 1),              // Unknown error
	HardwareIOError = (1 << 2),      // Communication error with HW
	NotInitialized = (1 << 3),       // Interface not initialized
	NotSupported = (1 << 4),         // Feature not supported
	NotCalibrated = (1 << 5),        // The radio is not calibrated
	TooEarly = (1 << 6),             // Timestamp is in the past
	TooLate = (1 << 7),              // Timestamp is in the future
	OutOfRange = (1 << 8),           // A requested parameter setting is out of range
	NotExact = (1 << 9),             // The affected value is not an exact match to the requested one
	DataLost = (1 << 10),            // Received data lost due to slow reads
	Saturation = (1 << 11),          // Data contain values outside of +/-1+/-j
	RFHardwareFail = (1 << 12),      // Failure in RF hardware
	RFHardwareChange = (1 << 13),    // Change in RF hardware, not outright failure
	EnvironmentalFault = (1 << 14),  // Environmental spec exceeded for radio HW
	InvalidPort = (1 << 15),         // Invalid port number
	Pending = (1 << 16),             // Operation is pending
	Cancelled = (1 << 17),           // Operation cancelled
	// Masks
	// Errors requiring radio or port shutdown
	FatalErrorMask = HardwareIOError | RFHardwareFail | EnvironmentalFault | Failure,
	// Errors that can be cleared
	ClearErrorMask = TooEarly | TooLate | NotExact | DataLost | Saturation | InvalidPort,
	// Errors that are specific to a single call
	LocalErrorMask = NotInitialized | NotCalibrated | TooEarly | TooLate | OutOfRange |
	    NotExact | DataLost | Saturation | RFHardwareChange | InvalidPort,
    };

    /**
     * Retrieve the radio device path
     * @param devicePath Destination buffer
     * @return Error code (0 on success)
     */
    virtual unsigned int getInterface(String& devicePath) const
	{ return NotSupported; }

    /**
     * Retrieve radio capabilities
     * @return RadioCapability pointer, 0 if unknown or not implemented
     */
    virtual const RadioCapability* capabilities() const
	{ return m_radioCaps; }

    /**
     * Initialize the radio interface.
     * Any attempt to transmit or receive prior to this operation will return NotInitialized.
     * @param params Optional parameters list
     * @return Error code (0 on success)
     */
    virtual unsigned int initialize(const NamedList& params = NamedList::empty()) = 0;

    /**
     * Set multiple intrface parameters.
     * Each command must start with 'cmd:' to allow the code to detect unhandled commands.
     * Command sub-params should not start with the prefix
     * @param params Parameters list
     * @param shareFate True to indicate all parameters share the fate
     *  (return on first error, except for Pending), false to process all
     * @return Error code (0 on success). Failed command(s) will be set in the list
     *  with cmd_name.code=error_code
     */
    virtual unsigned int setParams(NamedList& params, bool shareFate = true) = 0;

    /**
     * Run internal calibration procedures and/or load calibration parmameters.
     * Any attempt to transmit or receive prior to this operation will return NotCalibrated.
     * @return Error code (0 on success)
     */
    virtual unsigned int calibrate()
	{ return NotSupported; }

    /**
     * Set the number of ports to be used
     * @param count The number of ports to be used
     * @return Error code (0 on success)
     */
    virtual unsigned int setPorts(unsigned count) const = 0;

    /**
     * Return any persistent error codes.
     * ("Persistent" means a condition of the radio interface itself,
     * as opposed to a status code related to a specific method call)
     * @param port Port number to check, or -1 for all ports OR'd together
     * @return Error(s) mask
     */
    virtual unsigned int status(int port = -1) const = 0;

    /**
     * Clear all error codes that can be cleared.
     * Note the not all codes are clearable, like HW failures for example.
     */
    virtual void clearErrors()
	{ m_lastErr &= ~ClearErrorMask; }

    /**
     * Send a frame of complex samples at a given time, interleaved IQ format.
     * If there are gaps in the sample stream, the RadioInterface must zero-fill.
     * All ports are sent together in interleaved format; example:
     * I0 Q0 I1 Q1 I2 Q2 I0 Q0 I1 Q1 I2 Q2, etc for a 3-port system
     * Block until the send is complete.
     * Return TooEarly if the blocking time would be too long.
     * Return TooLate if any of the block is in the past.
     * @param when Time to schedule the transmission
     * @param samples Data to send (array of 2 * size * ports floats, interleaved IQ)
     * @param size The number of sample periods in the samples array
     * @param powerScale Optional pointer to power scale value
     * @return Error code (0 on success)
     */
    virtual unsigned int send(uint64_t when, float* samples, unsigned size,
	float* powerScale = 0) = 0;

    /**
     * Receive the next available samples and associated timestamp.
     * All ports are received together in interleaved format.
     *  e.g: I0 Q0 I1 Q1 I2 Q2 I0 Q0 I1 Q1 I2 Q2, etc for a 3-port system.
     * The method will wait for proper timestamp (to be at least requested timestamp).
     * The caller must increase the timestamp after succesfull read.
     * The method may return less then requested size.
     * @param when Input: current timestamp. Output: read data timestamp
     * @param samples Destination buffer (array of 2 * size * ports floats, interleaved IQ)
     * @param size Input: requested number of samples. Output: actual number of read samples
     * @return Error code (0 on success)
     */
    virtual unsigned int recv(uint64_t& when, float* samples, unsigned& size) = 0;

    /**
     * Receive the next available samples and associated timestamp.
     * Compensate timestamp difference.
     * Copy any valid data in the future to auxiliary buffers.
     * Adjust the timestamp (no need for the caller to do it).
     * Handles buffer rotation also.
     * All sample counters (length/offset) are expected in sample periods.
     * @param when Input: current timestamp. Output: next read data timestamp
     * @param bufs Buffers to use
     * @param skippedBufs The number of skipped full buffers
     * @return Error code (0 on success)
     */
    virtual unsigned int read(uint64_t& when, RadioReadBufs& bufs,
	unsigned int& skippedBufs);

    /**
     * Get the current radio time at the MSFE converter
     * @param when Destination buffer for requested radio time
     * @return Error code (0 on success)
     */
    virtual unsigned int getTime(uint64_t& when) const = 0;

    /**
     * Get the time of the data currently being received from the radio
     * @param when Destination buffer for requested radio time
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxTime(uint64_t& when) const = 0;

    /**
     * Get the time of the the data currently being sent to the radio
     * @param when Destination buffer for requested radio time
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxTime(uint64_t& when) const = 0;

    /**
     * Set the frequency offset
     * @param offs Frequency offset to set
     * @param newVal Optional pointer to value set by interface (requested value may
     *  be adjusted to fit specific device value)
     * @return Error code (0 on success)
     */
    virtual unsigned int setFreqOffset(int offs, int* newVal = 0) = 0;

    /**
     * Set the sample rate
     * @param hz Sample rate value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int setSampleRate(uint64_t hz) = 0;

    /**
     * Get the actual sample rate
     * @param hz Sample rate value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int getSampleRate(uint64_t& hz) const = 0;

    /**
     * Set the anti-aliasing filter BW
     * @param hz Anti-aliasing filter value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int setFilter(uint64_t hz) = 0;

    /**
     * Get the actual anti-aliasing filter BW
     * @param hz Anti-aliasing filter value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int getFilterWidth(uint64_t& hz) const = 0;

    /**
     * Set the transmit frequency in Hz
     * @param hz Transmit frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxFreq(uint64_t hz) = 0;

    /**
     * Readback actual transmit frequency
     * @param hz Transmit frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxFreq(uint64_t& hz) const = 0;

    /**
     * Set the output power in dBm.
     * This is power per active port, compensating for internal gain differences.
     * @param dBm The output power to set
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxPower(unsigned dBm) = 0;

    /**
     * Set the receive frequency in Hz
     * @param hz Receive frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxFreq(uint64_t hz) = 0;

    /**
     * Readback actual receive frequency
     * @param hz Receive frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxFreq(uint64_t& hz) const = 0;

    /**
     * Set the input gain reference level in dBm.
     * This sets the satuation point of the receiver in dBm.
     * @param dBm Input gain value
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxSaturationPoint(int dBm)
	{ return NotSupported; }

    /**
     * Get the current rx gain reference level in dBm
     * @param dBm Receive gain value
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxSaturationPoint(int& dBm) const
	{ return NotSupported; }

    /**
     * Get the expected receiver noise floor in dBm.
     * This takes into account all noise sources in the receiver:
     * - thermal + noise figure
     * - quantization noise
     * @param dBm Receiver noise floor value
     * @return Error code (0 on success)
     */
    virtual unsigned int getExpectedNoiseFloor(int& dBm) const
	{ return NotSupported; }

    /**
     * Put the radio into normal mode
     * @return Error code (0 on success)
     */
    virtual unsigned int setRadioNormal()
	{ return NotSupported; }

    /**
     * Set software loopback inside the interface class
     * @return Error code (0 on success)
     */
    virtual unsigned int setSRadioLoopbackSW()
	{ return NotSupported; }

    /**
     * Set hardware loopback inside the radio, pre-mixer
     * @return Error code (0 on success)
     */
    virtual unsigned int setRadioLoopbackBaseband()
	{ return NotSupported; }

    /**
     * Set hardware loopback inside the radio, post-mixer
     * @return Error code (0 on success)
     */
    virtual unsigned int setRadioLoopbackRF()
	{ return NotSupported; }

    /**
     * Turn off modulation, and/or send DC
     * @return Error code (0 on success)
     */
    virtual unsigned int setRadioUnmodulated(float i = 0.0F, float q = 0.0F)
	{ return NotSupported; }

    virtual unsigned int getPowerConsumption(float& watts) const
	{ return NotSupported; }

    virtual unsigned int getPowerOutput(float& dBm, unsigned port) const
	{ return NotSupported; }

    virtual unsigned int getVswr(float& dB, unsigned port) const
	{ return NotSupported; }

    virtual unsigned int getTemperature(float& degrees) const
	{ return NotSupported; }

    /**
     * Calibration. Set the transmit pre-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxGain1(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Calibration. Set the transmit post-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxGain2(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Calibration. Automatic tx gain setting
     * @return Error code (0 on success)
     */
    virtual unsigned int autocalTxGain() const
	{ return NotSupported; }

    /**
     * Calibration. Set the receive pre-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxGain1(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Calibration. Set the receive post-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxGain2(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Calibration. Automatic rx gain setting
     * @return Error code (0 on success)
     */
    virtual unsigned int autocalRxGain() const
	{ return NotSupported; }

    /**
     * Calibration. Automatic tx/rx gain setting
     * Set post mixer value. Return a value to be used by upper layer
     * @param tx Direction
     * @param val Value to set
     * @param port Port to use
     * @param newVal Optional pointer to value to use for calculation by the upper layer
     * @return Error code (0 on success)
     */
    virtual unsigned int setGain(bool tx, int val, unsigned int port,
	int* newVal = 0) const
	{ return NotSupported; }

    // Frequency calibration.
    // The "val" here is a integer tuning control
    // on a scale that is specific to the hardware.
    // There are readback and write methodss to support a closed-loop search.
    // There is also a method to report the mean freq control step size.

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getFreqCal(int& val) const 
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setFreqCal(int val)
	{ return NotSupported; }

    /**
     * For automatic frequency calibration, the radio is presented
     * with a carrier at a calibrated feqeuency.
     * @return Error code (0 on success)
     */
    virtual unsigned int autoCalFreq(uint64_t refFreqHz)
	{ return NotSupported; }

    /**
     * Calibration. Automatic TX/RX DC calibration
     * @return Error code (0 on success)
     */
    virtual unsigned int autocalDCOffsets()
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxIOffsetCal(int& val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxQOffsetCal(int& val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxIOffsetCal(int& val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxQOffsetCal(int& val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxIOffsetCal(int val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxQOffsetCal(int val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxIOffsetCal(int val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxQOffsetCal(int val, unsigned port) const
	{ return NotSupported; }

    /**
     * Automatic IQ offset calibration, if the hardware supports it.
     * @param port Port number, -1 for all ports
     * @return Error code (0 on success)
     */
    virtual unsigned int autocalIQOffset(int port = -1)
	{ return NotSupported; }

    // The balance control is a single gain ratio expressed in dB
    // The "val" here is a integer balance control
    // on a scale that is specific to the hardware.
    // There are readback and write methods to support a closed-loop search.
    // There is also a method to report the mean control step size.
    // This calibration is done on each port.
    //
    // This can be implemented in software if the hardware does not support it.

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getIQBalanceCal(int& val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setIQBalanceCal(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Automatic IQ balance calibration, if the hardware supports it.
     * @param port Port number, or -1 for all ports
     * @return Error code (0 on success)
     */
    virtual unsigned int autoCalIQBalance(int port = -1)
	{ return NotSupported; }

    // The phase offset control calibrates relative phase across ports.
    // The "val" here is a integer balance control
    // on a scale that is specific to the hardware.
    // There are readback and write methods to support a closed-loop search.
    // There is also a method to report the mean control step size.
    // This calibration is done on each port.
    //
    // This can be implemented in software if the hardware does not support it.

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxPhaseCal(int& val, unsigned port) const 
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxPhaseCal(int& val, unsigned port) const 
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxPhaseCal(int val, unsigned port)
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxPhaseCal(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Automatic phase calibration across all ports, if the hardware supports it.
     * A set offset will also be added to all ports.
     * @return Error code (0 on success)
     */
    virtual unsigned int autoCalPhase(float baseRadians = 0.0F)
	{ return NotSupported; }

    // The I-Q delay control calibrates relative I-Q timing across ports.
    // The "val" here is a integer balance control
    // on a scale that is specific to the hardware.
    // There are readback and write methods to support a closed-loop search.
    // There is also a method to report the mean control step size.
    // This calibration is done on each port.
    //
    // This can be implemented in software if the hardware does not support it.

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int getIQDelayCal(int& val, unsigned port) const
	{ return NotSupported; }

    /**
     * @return Error code (0 on success)
     */
    virtual unsigned int setIQDelayCal(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Automatic IQ balance calibration, if the hardware supports it.
     * @param port Port number, or -1 for all ports
     * @return Error code (0 on success)
     */
    virtual unsigned int autoCalIQDelay(int port = -1)
	{ return NotSupported; }

    /**
     * Retrieve the interface name
     * @return Interface name
     */
    virtual const String& toString() const;

    /**
     * Retrieve the error string associated with a specific code
     * @param code Error code
     * @param defVal Optional default value to retrieve if not found
     * @return Valid TokenDict pointer
     */
    static inline const char* errorName(int code, const char* defVal = 0)
	{ return lookup(code,errorNameDict(),defVal); }

    /**
     * Retrieve the error name dictionary
     * @return Valid TokenDict pointer
     */
    static const TokenDict* errorNameDict();

protected:
    /**
     * Constructor
     * @param name Interface name
     */
    inline RadioInterface(const char* name)
	: m_lastErr(0), m_totalErr(0), m_radioCaps(0), m_name(name)
	{ debugName(m_name); }

    unsigned int m_lastErr;               // Last error that appeared during functioning
    unsigned int m_totalErr;              // All the errors that appeared
    RadioCapability* m_radioCaps;         // Radio capabilities

private:
    String m_name;
};

}; // namespace TelEngine

#endif /* __YATERADIO_H */

/* vi: set ts=8 sw=4 sts=4 noet: */