/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 2000 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#include "nsScreenOS2.h"

#define INCL_PM
#include <os2.h>


nsScreenOS2 :: nsScreenOS2 (  )
{
  NS_INIT_REFCNT();

  // nothing else to do. I guess we could cache a bunch of information
  // here, but we want to ask the device at runtime in case anything
  // has changed.
}


nsScreenOS2 :: ~nsScreenOS2()
{
  // nothing to see here.
}


// addref, release, QI
NS_IMPL_ISUPPORTS(nsScreenOS2, NS_GET_IID(nsIScreen))


NS_IMETHODIMP
nsScreenOS2 :: GetRect(PRInt32 *outLeft, PRInt32 *outTop, PRInt32 *outWidth, PRInt32 *outHeight)
{
  *outTop = 0;
  *outLeft = 0;
  *outWidth = WinQuerySysValue( HWND_DESKTOP, SV_CXFULLSCREEN );
  *outHeight = WinQuerySysValue( HWND_DESKTOP, SV_CYFULLSCREEN ); 
#ifdef THIS_DOESNT_WORK
  HWND hwndDesktop = WinQueryDesktopWindow(NULLHANDLE, NULLHANDLE); 
  HDC hdc = WinQueryWindowDC(hwndDesktop);
  if (!hdc) {
     hdc = WinOpenWindowDC(hwndDesktop);
  } /* endif */

  LONG alArray[2];
  DevQueryCaps(hdc, CAPS_HORIZONTAL_RESOLUTION, CAPS_VERTICAL_RESOLUTION, alArray);

  *outWidth = alArray[0];
  *outHeight = alArray[1];
#endif

  return NS_OK;
  
} // GetRect


NS_IMETHODIMP
nsScreenOS2 :: GetAvailRect(PRInt32 *outLeft, PRInt32 *outTop, PRInt32 *outWidth, PRInt32 *outHeight)
{
  *outTop = 0;
  *outLeft = 0;
  *outWidth = WinQuerySysValue( HWND_DESKTOP, SV_CXFULLSCREEN );
  *outHeight = WinQuerySysValue( HWND_DESKTOP, SV_CYFULLSCREEN ); 

  return NS_OK;
  
} // GetAvailRect


NS_IMETHODIMP 
nsScreenOS2 :: GetPixelDepth(PRInt32 *aPixelDepth)
{
#ifdef THIS_DOESNT_WORK
  LONG lCap;

  HWND hwndDesktop = WinQueryDesktopWindow(NULLHANDLE, NULLHANDLE); 
  HDC hdc = WinQueryWindowDC(hwndDesktop);
  if (!hdc) {
     hdc = WinOpenWindowDC(hwndDesktop);
  } /* endif */

  DevQueryCaps(hdc, CAPS_COLOR_BITCOUNT, CAPS_COLOR_BITCOUNT, &lCap);

  *aPixelDepth = (PRInt32)lCap;
#endif
  *aPixelDepth = 0;

  return NS_OK;

} // GetPixelDepth


NS_IMETHODIMP 
nsScreenOS2 :: GetColorDepth(PRInt32 *aColorDepth)
{
  return GetPixelDepth ( aColorDepth );

} // GetColorDepth


