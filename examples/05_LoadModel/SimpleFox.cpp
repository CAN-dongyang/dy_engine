#include "Application.h"

int main()
{
	dy::Application app("SimpleFox");
	while(app.BeginFrame())
	{
		app.EndFrame();
		break;
	}
	return 0;
}
