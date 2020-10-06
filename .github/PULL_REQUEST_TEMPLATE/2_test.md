---
name: New App Test
about: This PR is adding a new app test. 

---

<!--- Provide a general summary of your changes in the Title above -->

## Description
<!--- Describe your changes in detail -->


## How Has This Been Tested?
<!--- Please describe in detail how you tested your changes. -->
<!--- Include details of your testing environment, and the tests you ran to -->
<!--- see how your change affects other areas of the code, etc. -->


## App Test Checklist 
<!--- What types of changes does your code introduce? Put an `x` in all the boxes that apply: -->
- [ ]  New .cpp/.h source files added as `<appname>.cpp/<appname>.h` in all lowercase.  
- [ ]  Contents of main.cpp moved into `<appname>.cpp` with a function named `<appname>` in lower camel case. 
- [ ]  Main.cpp replaced with call to app function in `<appname>.cpp` 
- [ ]  New app tests added as `ISIS3/isis/tests/FunctionalTests<appname>.cpp` in upper camel case
- [ ]  New GTests prefixed with `FunctionalTest<appname>`
- [ ]  Old Makefile tests removed 
- [ ]  New tests passing on Jenkins
- [ ]  Updated spreadsheat

## USGS Checklist:
<!--- Go over all the following points, and put an `x` in all the boxes that apply. -->
<!--- If you're unsure about any of these, don't hesitate to ask. We're here to help! -->
<!--- - [ ] My code follows the code style of this project. -->
- [ ] I have read and agree to abide by the [Code of Conduct](https://usgs-astrogeology.github.io/code/)
- [ ] I have read the [**CONTRIBUTING**](https://github.com/USGS-Astrogeology/ISIS3/blob/dev/CONTRIBUTING.md) document.
- [ ] My change requires a change to the documentation.
- [ ] I have updated the documentation accordingly.
- [ ] I have added tests to cover my changes.
- [ ] All new and existing tests passed.
- [ ] I have added myself to the [.zenodo.json](https://github.com/USGS-Astrogeology/ISIS3/blob/dev/.zenodo.json) document.
- [ ] I have added any user impacting changes to the [CHANGELOG.md](https://github.com/USGS-Astrogeology/ISIS3/blob/dev/CHANGELOG.md) document.vv
## Licensing
This project is mostly composed of free and unencumbered software released into the public domain, and we are unlikely to accept contributions that are not also released into the public domain. Somewhere near the top of each file should have these words:

> This work is free and unencumbered software released into the public domain. In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any and all copyright interest in the software to the public domain.

- [ ] I dedicate any and all copyright interest in this software to the public domain. I make this dedication for the benefit of the public at large and to the detriment of my heirs and successors. I intend this dedication to be an overt act of relinquishment in perpetuity of all present and future rights to this software under copyright law.
