# Hide Your Taskbar

A lightweight Windows 11 app for changing the look of your taskbar. It runs in the system tray and applies your chosen appearance to every display. Tested on Windows 11 26H2 x64, build **26300.10075**. Compatibility may vary across Windows builds.

## Download and run

The portable release runs without Visual Studio, C++ build tools, the Windows SDK, or the Visual C++ Redistributable.

1. Download `HideYourTaskbar-windows-x64.zip` from **Releases** and extract it to a folder.
2. Keep `TaskbarAppearance.dll` in the same folder as `HideYourTaskbar.exe`, then double-click the EXE.
3. Double-click the **H** icon near the clock, or right-click it and select **Settings**.

Choose **Default**, **Transparent**, **Blur**, or **Color**. Changes take effect immediately, and the app remembers your choice. Under **Advanced settings**, you can set a separate appearance for when the active window is maximized or full screen, or enable **Start with Windows**. Choose **Exit** from the tray menu to restore the previous taskbar appearance.

In **Color** mode, pick a tint from the color field or enter a HEX value, then select **Apply**. Adjust **Opacity** from 0% (fully transparent) to 100% (solid color). The taskbar updates as you move the slider.

To remove the app, turn off **Start with Windows**, exit the app, and remove its folder.

## Privacy

The app works offline. Your appearance and startup preferences are stored locally in your Windows user account.

## Build from source

Install the **Desktop development with C++** workload in Visual Studio, including the MSVC x64/x86 tools and the Windows 11 SDK. Open the **x64 Native Tools Command Prompt**, go to the project folder, and run:

```cmd
Build.cmd build
```

The build writes its output to the `build` folder. Include `HideYourTaskbar.exe` and `TaskbarAppearance.dll` in the release ZIP. List the Windows build used for testing in the release notes.

- `src/app` contains the app entry point, tray menu, settings, and session management.
- `src/taskbar` contains the XAML code that changes the taskbar appearance.
- `src/shared` defines the session data shared by the app and its XAML component.
- `resources` contains the settings dialog and app icon.

## Contributing

See the [contributing guide](CONTRIBUTING.md) to report a bug or propose a change.

## Technical references

The app uses XAML Diagnostics and Windows brushes to change the taskbar appearance. Windows updates can affect compatibility. [Microsoft XAML Diagnostics](https://learn.microsoft.com/en-us/windows/win32/api/_xaml_diagnostics/) · [TranslucentTB taskbar compatibility fix](https://github.com/TranslucentTB/TranslucentTB/commit/b8716c1)

