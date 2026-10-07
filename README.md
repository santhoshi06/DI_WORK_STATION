# This doc explains method to setup DI work Station in Windows Machine

1. First Install MSYS2 package (application file to install you can get in doc folder)

2. Then open MSYS2 terminal and in that run below commands, this will install compiler and some deps for our code-
```
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-make
pacman -S mingw-w64-ucrt-x86_64-libwebsockets
pacman -S mingw-w64-ucrt-x86_64-cjson
pacman -S make
```

3. After these package installation we have to add 2 below paths in 'System Environment Variables', For this -

```
Search 'Edit the system environment varibales' -> Environment Variables -> System variables -> Path -> New 
    Here add two new paths-
        C:\msys64\ucrt64\bin
        C:\msys64\usr\bin
```

4. Install Quectel USB driver to make COM ports to detect. for that follow installation step given in 'quectel_windows_usb_driverq_ndis_v2-8_en.zip' file.

5. Now goto source code path run make, it will generate 'di_work.exe' file, which is our dialing application.

6. Now almost all ready, lastly we have to make our code application i.e. 'di_work.exe' to run automatically on boot, for this follow document 'how_to_start_di_work_application_on_boot' given in doc folder.

Now full setup is done.
