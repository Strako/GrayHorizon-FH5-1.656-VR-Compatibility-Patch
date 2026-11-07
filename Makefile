# GrayHorizon FH5 1.656 proxy build
# Run from an x64 Native Tools Command Prompt for Visual Studio:
#   nmake /f Makefile

CXX      = cl
CXXFLAGS = /nologo /LD /O2 /EHsc
LDFLAGS  = /link /OUT:dist\xinput1_3.dll user32.lib

TARGET = dist\xinput1_3.dll
SOURCE = xinput1_3.cpp

all: $(TARGET)

$(TARGET): $(SOURCE)
	@if not exist dist mkdir dist
	$(CXX) $(CXXFLAGS) $(SOURCE) $(LDFLAGS)

clean:
	@if exist dist\xinput1_3.dll del /q dist\xinput1_3.dll
	@if exist xinput1_3.obj del /q xinput1_3.obj
	@if exist xinput1_3.exp del /q xinput1_3.exp
	@if exist xinput1_3.lib del /q xinput1_3.lib

.PHONY: all clean
