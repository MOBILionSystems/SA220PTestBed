// readTemp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "AqMD3.h"
using std::runtime_error;
using std::cerr;
using std::hex;

#define checkApiCall( f ) do { ViStatus s = f; testApiCall( s, #f ); } while( false )

// Edit resource and options as needed. Resource is ignored if option has Simulate=true.
// An input signal is necessary if the example is run in non simulated mode, otherwise
// the acquisition will time out.
//ViChar resource[] = "PXI79::0::0::INSTR";
//ViChar options[] = "Simulate=true, DriverSetup= Model=U5303A";


//ViInt64 const recordSize = 1000000;
//ViInt64 const numRecords = 1;

// Utility function to check status error during driver API call.
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

int main()
{
    std::cout << "Hello World!\n";
    ViSession session;
    ViStatus status;
    status = AqMD3_InitWithOptions(ViString("PXI79::0::0::INSTR"), VI_TRUE, VI_FALSE, "", &session);

    ViBoolean simulate;
    checkApiCall(AqMD3_GetAttributeViBoolean(session, "", AQMD3_ATTR_SIMULATE, &simulate));
    std::cout << "\nSimulate:           " << (simulate ? "True" : "False") << '\n';

    
    
    for (int i = 0; i < 20; i++) {
        //checkApiCall(AqMD3_QueryBoardTemperature(session, &temperture));
        ViReal64 temperture1 = 0;
        AqMD3_QueryBoardTemperature(session, &temperture1);
        std::cout << "Board Temperature1: " << temperture1 << std::endl;

        ViReal64 temperture2 = 0;
        AqMD3_GetAttributeViReal64(session, VI_NULL, AQMD3_ATTR_BOARD_TEMPERATURE, &temperture2);
        std::cout << "Board Temperature2: " << temperture2 << std::endl;
    }
    status = AqMD3_close(session);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
