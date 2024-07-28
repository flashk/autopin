# autopin

![autopin](https://github.com/user-attachments/assets/aed70eaf-6245-4c08-aaa5-fdc3a917ce0c)

`autopin` is a lightweight Windows command line tool that can be used to automatically enter the PIN when using `signtool` to sign files with a hardware key (e.g. Yubikey). This allows for unattended builds when signing many files. I created this after being unsatisfied with the other solutions I found. Some were closed source binaries or required significant changes to my existing workflow for signing files. `autopin` can be used to easily update existing workflows that use `signtool`.

## Usage
```
autopin [pin] [signtool_path] [signtool_args...]
```

Example:
```
autopin 123456 signtool.exe sign /sha1 thumbprint /fd SHA256 /t http://timestamp.digicert.com myapp.exe
```

## How it works

`autopin` is run through the command line. The first argument is the PIN for your hardware key. The remaining arguments are your existing `signtool.exe` command line. Technically, `autopin` can work with any other command line tool that pops up a PIN input dialog, but I've only tested with `signtool.exe`

When executed, `autopin` will spawn a process using the provided command line and monitor the process for any dialog windows. When it detects a dialog with a password field, it will enter the provided PIN and automatically invoke the OK button on the dialog. This is all done using the `IUIAutomation` COM interface.
