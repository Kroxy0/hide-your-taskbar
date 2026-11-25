#include "TaskbarSession.h"
#include "TaskbarBrush.h"
#include <ocidl.h>
#include <xamlom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <string_view>

namespace xaml = winrt::Windows::UI::Xaml;

namespace
{
bool IsTaskbarBackgroundPart(std::wstring_view name)
{
    return name == L"BackgroundFill" || name == L"BackgroundStroke";
}

using Dispatcher = winrt::Windows::UI::Core::CoreDispatcher;
using Priority = winrt::Windows::UI::Core::CoreDispatcherPriority;

struct Background
{
    Background(const xaml::Shapes::Rectangle& rectangle, bool isBorder, LONG64 style)
        : dispatcher(rectangle.Dispatcher()), appearance(rectangle), border(isBorder)
    {
        appearance.Apply(style, border);
    }

    Dispatcher dispatcher;
    TaskbarBrush appearance;
    bool border;
};

using Backgrounds = std::map<InstanceHandle, std::shared_ptr<Background>>;

struct TaskbarDiagnostics : winrt::implements<TaskbarDiagnostics, IObjectWithSite, IVisualTreeServiceCallback2>
{
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) noexcept override try
    {
        if (!site || diagnostics_)
            return S_OK;

        winrt::com_ptr<IUnknown> unknown;
        unknown.copy_from(site);
        diagnostics_ = unknown.as<IXamlDiagnostics>();
        std::thread([self = get_strong()] { self->Watch(); }).detach();
        return S_OK;
    }
    catch (...) { return winrt::to_hresult(); }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return diagnostics_ ? diagnostics_->QueryInterface(iid, result) : E_FAIL;
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element,
                                                VisualMutationType mutation) noexcept override try
    {
        using OwnedString = std::unique_ptr<wchar_t, decltype(&SysFreeString)>;
        const OwnedString file(element.SrcInfo.FileName, SysFreeString);
        const OwnedString hash(element.SrcInfo.Hash, SysFreeString);
        const OwnedString name(element.Name, SysFreeString);
        const OwnedString type(element.Type, SysFreeString);

        if (mutation == Remove)
        {
            std::scoped_lock lock(mutex_);
            backgrounds_.erase(element.Handle);
        }
        else if (mutation == Add && name && IsTaskbarBackgroundPart(name.get()) &&
                 type && std::wstring_view(type.get()) == winrt::name_of<xaml::Shapes::Rectangle>())
        {
            winrt::com_ptr<::IInspectable> instance;
            winrt::check_hresult(diagnostics_->GetIInspectableFromHandle(element.Handle, instance.put()));
            const auto rectangle = instance.try_as<xaml::Shapes::Rectangle>();
            if (IsInsideTaskbarFrame(rectangle))
            {
                std::scoped_lock lock(mutex_);
                if (active_ && !backgrounds_.contains(element.Handle))
                {
                    const LONG64 style = InterlockedCompareExchange64(&stats_->appearance, 0, 0);
                    backgrounds_.try_emplace(element.Handle, std::make_shared<Background>(
                        rectangle, std::wstring_view(name.get()) == L"BackgroundStroke", style));
                    InterlockedIncrement(&stats_->applied);
                    if (applied_)
                        SetEvent(applied_.get());
                }
            }
        }
        return S_OK;
    }
    catch (...) { return winrt::to_hresult(); }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState,
                                                    LPCWSTR) noexcept override
    {
        return S_OK;
    }

private:
    template<class Action>
    void OnBackgroundThreads(const Backgrounds& backgrounds, Action action)
    {
        for (const auto& entry : backgrounds)
        {
            const auto& background = entry.second;
            try
            {
                background->dispatcher.RunAsync(Priority::Normal, [background, action, stats = stats_.get()]
                {
                    try { action(*background, *stats); }
                    catch (...) { InterlockedIncrement(&stats->errors); }
                }).get();
            }
            catch (...) { InterlockedIncrement(&stats_->errors); }
        }
    }

    void Watch() noexcept
    {
        const winrt::handle stop(OpenEventW(SYNCHRONIZE, FALSE, TaskbarStop));
        const winrt::handle done(OpenEventW(EVENT_MODIFY_STATE, FALSE, TaskbarDone));
        const winrt::handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, TaskbarReady));
        const winrt::handle controller(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, TaskbarMutex));
        const winrt::handle mapping(OpenFileMappingW(FILE_MAP_WRITE, FALSE, TaskbarStatsName));
        const winrt::handle changed(OpenEventW(SYNCHRONIZE, FALSE, TaskbarChanged));
        const winrt::handle updated(OpenEventW(EVENT_MODIFY_STATE, FALSE, TaskbarUpdated));
        if (!stop || !done || !ready || !controller || !changed || !updated || !mapping)
            return;

        try
        {
            winrt::init_apartment();
            const auto tree = diagnostics_.as<IVisualTreeService>();
            stats_.reset(static_cast<TaskbarStats*>(MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, sizeof(TaskbarStats))));
            if (!stats_)
            {
                SetEvent(done.get());
                return;
            }
            applied_.attach(OpenEventW(EVENT_MODIFY_STATE, FALSE, TaskbarApplied));
            winrt::check_hresult(tree->AdviseVisualTreeChange(this));
            SetEvent(ready.get());

            const HANDLE signals[]{stop.get(), controller.get(), changed.get()};
            DWORD signal;
            while ((signal = WaitForMultipleObjects(ARRAYSIZE(signals), signals, FALSE, INFINITE)) == WAIT_OBJECT_0 + 2)
            {
                Backgrounds backgrounds;
                {
                    std::scoped_lock lock(mutex_);
                    backgrounds = backgrounds_;
                }
                const LONG64 style = InterlockedCompareExchange64(&stats_->appearance, 0, 0);
                OnBackgroundThreads(backgrounds, [style](Background& background, TaskbarStats&)
                {
                    background.appearance.Apply(style, background.border);
                });
                SetEvent(updated.get());
            }
            if (signal == WAIT_OBJECT_0 + 1 || signal == WAIT_ABANDONED_0 + 1)
                ReleaseMutex(controller.get());

            Backgrounds backgrounds;
            {
                std::scoped_lock lock(mutex_);
                active_ = false;
                backgrounds.swap(backgrounds_);
            }

            OnBackgroundThreads(backgrounds, [](Background& background, TaskbarStats& stats)
            {
                background.appearance.Restore();
                InterlockedIncrement(&stats.restored);
            });
            tree->UnadviseVisualTreeChange(this);
            winrt::uninit_apartment();
        }
        catch (...) {}
        SetEvent(done.get());
    }

    winrt::com_ptr<IXamlDiagnostics> diagnostics_;
    winrt::handle applied_;
    std::unique_ptr<TaskbarStats, UnmapTaskbarStats> stats_;
    std::recursive_mutex mutex_;
    Backgrounds backgrounds_;
    bool active_{true};
};

struct SiteFactory : winrt::implements<SiteFactory, IClassFactory>
{
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) noexcept override try
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        return winrt::make_self<TaskbarDiagnostics>()->QueryInterface(iid, result);
    }
    catch (...) { return winrt::to_hresult(); }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) try
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (clsid != TaskbarSiteId)
        return CLASS_E_CLASSNOTAVAILABLE;
    return winrt::make_self<SiteFactory>()->QueryInterface(iid, result);
}
catch (...) { return winrt::to_hresult(); }
