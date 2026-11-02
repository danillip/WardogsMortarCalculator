#include "../Shared/WindowsIncludes.h"
#include "StartupCheck.h"
#include <appmodel.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>
#include <format>

namespace wardogs {
namespace {
enum class Availability { Present,Missing,Unknown };
enum ButtonId { Continue=100,LanguageSettings=101,Documentation=102,CheckAgain=103,Exit=104 };
struct Status {
    Availability identity=Availability::Unknown;
    Availability englishOcr=Availability::Unknown;
    Availability engine=Availability::Unknown;
    std::wstring error;
    bool Ready() const {return identity==Availability::Present&&englishOcr==Availability::Present&&engine==Availability::Present;}
};
Status Probe() {
    Status status;
    UINT32 length=0;
    const LONG packageResult=GetCurrentPackageFullName(&length,nullptr);
    if(packageResult==ERROR_INSUFFICIENT_BUFFER&&length>0)status.identity=Availability::Present;
    else if(packageResult==APPMODEL_ERROR_NO_PACKAGE)status.identity=Availability::Missing;
    else status.error=L"GetCurrentPackageFullName: "+std::to_wstring(packageResult);
    try {
        using winrt::Windows::Media::Ocr::OcrEngine;
        winrt::Windows::Globalization::Language language(L"en-US");
        if(OcrEngine::IsLanguageSupported(language)) {
            status.englishOcr=Availability::Present;
            auto engine=OcrEngine::TryCreateFromLanguage(language);
            status.engine=engine?Availability::Present:Availability::Missing;
        } else {
            // Outside the supported packaged context, false does not prove that
            // the English feature is absent from Windows itself.
            status.englishOcr=status.identity==Availability::Present?Availability::Missing:Availability::Unknown;
            status.engine=Availability::Missing;
        }
    } catch(winrt::hresult_error const& error) {
        status.error+=std::format(L"\nWindows OCR: 0x{:08X} — {}",static_cast<uint32_t>(error.code().value),error.message().c_str());
    } catch(...) {
        status.error+=L"\nWindows OCR: probe failed";
    }
    return status;
}
struct Dialog {
    bool english;
    Status status;
    std::wstring Text(wchar_t const* ru,wchar_t const* en) const {return english?en:ru;}
    std::wstring Content() const {
        std::wstring content=Text(L"Регистрация приложения (package identity): ",L"Application package identity: ");
        content+=status.identity==Availability::Present?Text(L"есть.",L"present."):status.identity==Availability::Missing?Text(L"нет — EXE запущен без пакета.",L"absent — the EXE is running unpackaged."):Text(L"не удалось проверить.",L"could not be checked.");
        content+=Text(L"\nEnglish OCR (en-US): ",L"\nEnglish OCR (en-US): ");
        content+=status.englishOcr==Availability::Present?Text(L"доступен.",L"available."):status.englishOcr==Availability::Missing?Text(L"недоступен. Добавьте English → Optical Character Recognition в параметрах языка Windows.",L"unavailable. Add English → Optical Character Recognition in Windows language settings."):Text(L"доступность не подтверждена. Это не доказывает отсутствие языкового компонента.",L"availability could not be confirmed. This does not prove that the language feature is missing.");
        content+=Text(L"\nСоздание движка OCR: ",L"\nOCR engine activation: ");
        content+=status.engine==Availability::Present?Text(L"успешно; само распознавание не проверялось.",L"successful; actual recognition was not tested."):status.engine==Availability::Missing?Text(L"недоступно этому приложению.",L"unavailable to this application."):Text(L"не подтверждено.",L"not confirmed.");
        if(status.identity!=Availability::Present)
            content+=Text(L"\n\nMicrosoft официально поддерживает Windows OCR в desktop-приложениях с package identity. Это регистрация приложения, а не отдельный компонент для скачивания. Установка English OCR не заменяет её. Даже при успешной проверке движка работа Portable без пакета не гарантируется.",L"\n\nMicrosoft officially supports desktop Windows OCR with package identity. Identity is application registration, not a downloadable component. Installing English OCR does not provide it. Even if engine activation succeeds, unpackaged Portable operation is not guaranteed.");
        content+=Text(L"\n\nПараметры Windows и браузер откроются только по нажатию кнопки. После установки компонента нажмите «Проверить снова». Можно продолжить запуск HUD; если OCR недоступен, остаётся резервный ручной ввод.",L"\n\nWindows Settings and the browser open only when you click a button. After installing the feature, click Check again. You can continue to the HUD; manual entry remains available if OCR cannot run.");
        if(!status.error.empty())content+=L"\n\n"+status.error;
        return content;
    }
};
void OpenPage(HWND owner,wchar_t const* uri,bool english) {
    // Fixed public URI, no session data or coordinates are included.
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(owner,L"open",uri,nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32){auto message=std::wstring(english?L"Could not open the page. Open this address manually:\n":L"Не удалось открыть страницу. Откройте этот адрес вручную:\n")+uri;MessageBoxW(owner,message.c_str(),L"WARDOGS Portable",MB_OK|MB_ICONINFORMATION);}
}
HRESULT CALLBACK Callback(HWND window,UINT notification,WPARAM wParam,LPARAM,LONG_PTR data) noexcept {
    if(notification!=TDN_BUTTON_CLICKED)return S_OK;
    auto& dialog=*reinterpret_cast<Dialog*>(data);
    try {
        if(wParam==LanguageSettings){OpenPage(window,L"ms-settings:regionlanguage",dialog.english);return S_FALSE;}
        if(wParam==Documentation){OpenPage(window,L"https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr",dialog.english);return S_FALSE;}
        if(wParam==CheckAgain){dialog.status=Probe();auto content=dialog.Content();SendMessageW(window,TDM_SET_ELEMENT_TEXT,TDE_CONTENT,reinterpret_cast<LPARAM>(content.c_str()));return S_FALSE;}
    } catch(...) {
        MessageBoxW(window,dialog.english?L"The check could not be completed. You can continue or close the application.":L"Не удалось выполнить проверку. Можно продолжить или закрыть приложение.",L"WARDOGS Portable",MB_OK|MB_ICONINFORMATION);
        return S_FALSE;
    }
    return S_OK;
}
}
bool ConfirmPortableStartup(HINSTANCE instance,bool english) {
    Dialog dialog{english,Probe()};
    if(dialog.status.Ready())return true;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    auto content=dialog.Content();
    const TASKDIALOG_BUTTON buttons[]{
        {LanguageSettings,english?L"Open Windows language settings\nInstall the English Optical Character Recognition feature.":L"Открыть параметры языка Windows\nУстановить английский компонент Optical Character Recognition."},
        {Documentation,english?L"Read Microsoft's OCR requirements\nOpen the official documentation in your browser.":L"Открыть требования Windows OCR\nДокументация Microsoft откроется в браузере."},
        {CheckAgain,english?L"Check again":L"Проверить снова"},
        {Continue,english?L"Continue to the HUD":L"Продолжить запуск HUD"},
        {Exit,english?L"Exit":L"Выйти"}
    };
    TASKDIALOGCONFIG config{};config.cbSize=sizeof(config);config.hInstance=instance;
    config.dwFlags=TDF_USE_COMMAND_LINKS|TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle=L"WARDOGS Portable";config.pszMainIcon=TD_INFORMATION_ICON;
    config.pszMainInstruction=english?L"Windows OCR startup check":L"Проверка Windows OCR перед запуском";
    config.pszContent=content.c_str();config.cButtons=static_cast<UINT>(std::size(buttons));config.pButtons=buttons;config.nDefaultButton=Continue;
    config.pfCallback=Callback;config.lpCallbackData=reinterpret_cast<LONG_PTR>(&dialog);config.cxWidth=420;
    int choice=Exit;
    if(FAILED(TaskDialogIndirect(&config,&choice,nullptr,nullptr)))
        return MessageBoxW(nullptr,content.c_str(),english?L"Continue to WARDOGS Portable?":L"Продолжить запуск WARDOGS Portable?",MB_YESNO|MB_ICONINFORMATION)==IDYES;
    return choice==Continue;
}
}
