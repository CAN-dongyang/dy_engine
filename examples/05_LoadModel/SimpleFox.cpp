#include "Application.h"
#include "FoxLightDemo.h"
#include "LoadModelOptions.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	try
	{
		dy::Examples::LoadModelOptions options;
		std::string optionError;
		if(!dy::Examples::ParseLoadModelOptions(argc, argv, options, optionError))
		{
			std::cerr << optionError << '\n';
			return -1;
		}

		dy::Application app("SimpleFox");
		dy::Examples::FoxLightDemo demo(app.GetScene(), app.GetRenderer());
		while(app.BeginFrame())
		{
			demo.Update(app.GetDeltaSeconds(), app.GetElapsedSeconds());
			app.EndFrame();
			if(options.smokeSeconds > 0.0f && app.GetElapsedSeconds() >= options.smokeSeconds) break;
		}
		return 0;
	}
	catch(const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return -1;
	}
}
