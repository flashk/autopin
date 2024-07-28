#include <windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <atlbase.h>
#include <UIAutomation.h>

// Time (ms) between searches for PIN entry window
#define RETRY_PIN_WINDOW_SEARCH_TIMEOUT 30

// Structure containing PIN entry parameters used with EnumWindows
typedef struct {
	DWORD dwProcessId;
	bool bPinEntered;
	std::wstring strPin;
	IUIAutomation* pAutomation;
} ProcessPinData;

/*
 Outputs id/name of all child elements of specified root element
 */
static void OutputElementChildrenInfo(IUIAutomation* pAutomation, IUIAutomationElement* pElement)
{
	// Create true condition to display all elements
	CComPtr<IUIAutomationCondition> conditionTrue;
	if(FAILED(pAutomation->CreateTrueCondition(&conditionTrue))) {
		return;
	}

	// Get all elements
	CComPtr<IUIAutomationElementArray > elements;
	if(FAILED(pElement->FindAll(TreeScope::TreeScope_Descendants, conditionTrue, &elements))) {
		return;
	}

	// Get length of element array
	int length;
	if(FAILED(elements->get_Length(&length))) {
		return;
	}

	// Iterate elements and output id/name
	for(int i = 0; i < length; ++i) {

		CComPtr<IUIAutomationElement> element;
		if(FAILED(elements->GetElement(i, &element))) {
			continue;
		}

		CComBSTR bstrId;
		element->get_CurrentAutomationId(&bstrId);
		std::wstring strId(bstrId, bstrId.Length());

		CComBSTR bstrName;
		element->get_CurrentName(&bstrName);
		std::wstring strName(bstrName, bstrName.Length());

		std::wcout << L"  id='" << strId << L"' name='" << strName << L"'" << std::endl;
	}
}

/*
 Invoke the OK button on the specified dialog element.
 Return if invocation was successfull.
 */
static bool InvokeDialogOkButton(IUIAutomation* pAutomation, IUIAutomationElement* pDialog)
{
	CComPtr<IUIAutomationElement> elementButton;

	// Search for button using list of possible conditions
	std::vector<std::pair<std::wstring, PROPERTYID>> buttonConditions;
	buttonConditions.push_back({L"OkButton", UIA_AutomationIdPropertyId});
	buttonConditions.push_back({L"OK", UIA_NamePropertyId});
	for(auto v : buttonConditions) {
		
		// Create condition for OK button
		CComVariant propVariant(v.first.c_str());
		CComPtr<IUIAutomationCondition> conditionButton;
		if(FAILED(pAutomation->CreatePropertyCondition(v.second, propVariant, &conditionButton))) {
			continue;
		}

		// Find first OK button element
		if(FAILED(pDialog->FindFirst(TreeScope::TreeScope_Descendants, conditionButton, &elementButton))) {
			continue;
		}

		// Stop searching if found button
		if(elementButton) {
			break;
		}
	}

	// Make sure we found the OK button
	if(!elementButton) {
		std::cerr << "Failed to find OK button (see list of dialog elements below):" << std::endl;
		OutputElementChildrenInfo(pAutomation, pDialog);
		return false;
	}

	// Create button invoke pattern
	CComPtr<IUIAutomationInvokePattern> invokePatternButton;
	if(FAILED(elementButton->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invokePatternButton)))) {
		std::cerr << "Failed get invoke pattern for OK button" << std::endl;
		return false;
	}

	// Invoke the button
	if(FAILED(invokePatternButton->Invoke())) {
		std::cerr << "Failed to invok OK button" << std::endl;
		return false;
	}

	return true;
}

/*
 EnumWindows callback function that checks if window belongs to
 spawned process and then searches for first password field element
 and inputs PIN if found.
 */
static BOOL CALLBACK FindPinWindowProc(HWND hWnd, LPARAM lParam)
{
	ProcessPinData* data = (ProcessPinData*)lParam;

	// Check if window belongs to calling process and is in the foreground
	DWORD processID = 0;
	GetWindowThreadProcessId(hWnd, &processID);
	if(processID != data->dwProcessId) {
		return TRUE;
	}

	// Get root element from HWND
	CComPtr<IUIAutomationElement> elementRoot;
	if(FAILED(data->pAutomation->ElementFromHandle(hWnd, &elementRoot))) {
		return TRUE;
	}

	// Create condition for password property
	CComVariant isPassword(true);
	CComPtr<IUIAutomationCondition> conditionPassword;
	if(FAILED(data->pAutomation->CreatePropertyCondition(UIA_IsPasswordPropertyId, isPassword, &conditionPassword))) {
		return TRUE;
	}

	// Find first password element
	CComPtr<IUIAutomationElement> elementPassword;
	if(FAILED(elementRoot->FindFirst(TreeScope::TreeScope_Descendants, conditionPassword, &elementPassword))) {
		return TRUE;
	}
	if(!elementPassword) {
		return TRUE;
	}
									
	// Get value pattern
	CComPtr<IUnknown> pattern;  
	if(FAILED(elementPassword->GetCurrentPattern(UIA_ValuePatternId, &pattern))) {
		return TRUE;
	}

	// Cast to IUIAutomationValuePattern interface 
	CComQIPtr<IUIAutomationValuePattern> valuePattern(pattern);
	if(!valuePattern) {
		return TRUE;
	}

	// Apply PIN
	CComBSTR bstrPin(data->strPin.c_str());
	if(FAILED(valuePattern->SetValue(bstrPin))) {
		return TRUE;
	}

	// Set flag that PIN was entered to stop search for PIN field
	data->bPinEntered = true;

	// Invoke OK button on window element
	InvokeDialogOkButton(data->pAutomation, elementRoot);
	
	return FALSE;
}

/*
 Enter the specified PIN into any dialog belonging to the specified process.
 Returns if the PIN was successfully entered.
 */
static bool EnterPINForProcess(HANDLE hProcess, const std::wstring& strPin)
{
	// Structure for passing process/Pin information to EnumWindows
	ProcessPinData data;
	data.bPinEntered = false;
	data.dwProcessId = GetProcessId(hProcess);
	data.strPin = strPin;
	
	// Initialize COM
	if(SUCCEEDED(CoInitialize(NULL))) {

		// Initialize automation handle
		CComPtr<IUIAutomation> automation;
		if(SUCCEEDED(automation.CoCreateInstance(CLSID_CUIAutomation))) {

			// Save automation pointer in structure
			data.pAutomation = automation;

			// Enumerate all top level windows until we find PIN entry field or process exits
			while(!data.bPinEntered && (WaitForSingleObject(hProcess, RETRY_PIN_WINDOW_SEARCH_TIMEOUT) != WAIT_OBJECT_0)) {
				EnumWindows(FindPinWindowProc, (LPARAM)&data);
			}

		} else {
			std::cerr << "Failed to create IUIAutomation interface" << std::endl;
		}
			
		// Release automation interface
		automation.Release();
			
		// Uninitialize COM
		CoUninitialize();

	} else {
		std::cerr << "Failed to initialize COM" << std::endl;
	}

	return data.bPinEntered;
}

int wmain()
{
	// Make sure we have enough arguments
	if(__argc < 3) {
		std::cerr << "Usage: autopin [pin] [command]" << std::endl;
		return 1;
	}

	// Get PIN from arguments
	std::wstring signPin = __wargv[1];

	// Create command line from remaining arguments
	std::wostringstream ssCmdLine;
	for(int i = 2; i < __argc; ++i) {
		ssCmdLine << L"\"" << __wargv[i] << L"\" ";
	}
	std::wstring strCmdLine = ssCmdLine.str();

	// Spawn requested process
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );

	si.dwFlags = STARTF_FORCEOFFFEEDBACK;

	BOOL bCreated = CreateProcessW(NULL,
					const_cast<wchar_t*>(strCmdLine.c_str()),
					NULL,
					NULL,
					FALSE,
					0,
					NULL,
					NULL,
					&si,
					&pi);

	if(!bCreated) {
		std::cerr << "Could not create process: " << GetLastError() << std::endl;
		return 1;
	}

	// Enter PIN into first detected password field belonging to process
	EnterPINForProcess(pi.hProcess, signPin);

	// Wait for process to finish
	WaitForSingleObject(pi.hProcess, INFINITE);
	
	// Get process exit code
	DWORD dwExitCode = 0;
	GetExitCodeProcess(pi.hProcess, &dwExitCode);

	// Close process handles
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	return dwExitCode;
}
