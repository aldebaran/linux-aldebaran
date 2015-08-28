README cgosmon
==============

The cgosmon utility demonstrates the access to the onboard hardware sensors via the CGOS interface.


DISCLAIMER
----------
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

Requirement
-----------
The tool is written for CGOS API rev. 1.2. and above.


Win32
-----
Build-Environment: Microsoft Visual Studio 2005
Build-Directory:   Win32

- open project in build environment 
- special build options:
   - disable "embedd manifest" from "Manifest Tool/Input and Output"
   - disable "precompiled header"
- select "Build cgosmon" from build menu
 

WindowsCE 5.0
-------------
Build-Environment: Platform Builder for Microsoft WindowsCE 5.0
Build-Directory:   CE

- copy the file cgos.lib (from file Cgos.zip, CE subfolder) to C:\WINCE500\PLATFORM\Congatec\lib\x86\retail
- during your project session inside the WindowsCE 5.0 Platform Builder
	- open console window
	- navigate to the build directory
	- execute the "build" command


WindowsCE 6.0
-------------
Build-Environment: Visual Studio 2005 & Platform Builder for Microsoft WindowsCE 6.0
Build-Directory:   -

- copy the file cgos.lib (from file Cgos.zip, CE subfolder) to C:\WINCE600\PLATFORM\Congatec\lib\x86\retail
- during your project session inside the WindowsCE 6.0 Platform Builder
	- open console window
	- navigate to the build directory
	- execute the "build" command


Linux
-----
Build-Environment: GNU compiler collection version 3.3.4 or above
Build-Directory:   Lx
 
- enter build directory
- execute "make" command

QNX
---
Build-Environment: GNU compiler collection version 2.95.3 or above
Build-Directory:   Qx
 
- enter build directory
- execute "make" command

	
(c) 2008, sml, congatec AG
