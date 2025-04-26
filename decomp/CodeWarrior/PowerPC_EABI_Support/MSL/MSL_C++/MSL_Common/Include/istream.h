/* Metrowerks Standard Library
 * Copyright ÃÂ¯ÃÂ½C 1995-2001 Metrowerks Corporation.  All rights
 * reserved.
 *
 * $Date: 2001/03/08 20:55:14 $
 * $Revision: 1.14 $
 */

// istream.h               // hh 971207 Changed filename from istream to istream.h

#ifndef _ISTREAM_H            // hh 971207 added standard include guards
#define _ISTREAM_H

#include <istream>

#ifndef _MSL_NO_CPP_NAMESPACE // hh 971207 Added backward compatibility
#ifndef _MSL_NO_IO
using std::basic_iostream;
using std::basic_istream;
using std::iostream;
using std::istream;
#ifndef _MSL_NO_WCHART_CPP_SUPPORT
using std::wiostream;
using std::wistream;
using std::ws;
#endif
#endif
#endif

#endif                        // _ISTREAM_H

// hh 971207 Changed filename from istream to istream.h
// hh 971207 added standard include guards
// hh 971207 Added backward compatibility
// hh 990120 changed name of MSIPL flags
// hh 991113 modified using policy
