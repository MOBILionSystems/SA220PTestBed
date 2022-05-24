///
/// Acqiris IVI-C Driver Example Program
///
/// Initializes the driver, reads a few Identity interface properties, and performs a
/// simple record acquisition.
///
/// For additional information on programming with IVI drivers in various IDEs, please see
/// http://www.ivifoundation.org/resources/
///
/// Runs in simulation mode without an instrument.
///

#include "Aq_sa220p.h"

int main()
{
    SA220P sa220p;
    sa220p.init();
    sa220p.configure();
    sa220p.calibrate();
    sa220p.acquire();
    sa220p.close();
    return 0;
}

