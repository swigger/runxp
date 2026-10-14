// Native desktop XAML glue, explicitly compiled without the UWP VS workload.
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#define _VSDESIGNER_DONT_LOAD_AS_DLL
#include "App.xaml.g.hpp"
#include "MainWindow.xaml.g.hpp"
#include "XamlTypeInfo.g.cpp"
#include "XamlTypeInfo.Impl.g.cpp"
#include "XamlMetaDataProvider.g.cpp"
