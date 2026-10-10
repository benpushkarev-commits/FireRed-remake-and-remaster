#include "launcher_model.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void launcher_binds_set_zapper(int mouse_enabled,int crosshair) { (void)mouse_enabled; (void)crosshair; }
int main(void) {
    static const char* const views[]={"Native","16:9","21:9","32:9","Adaptive"};
    RecompLauncherCNetplayCallbacks callbacks={0};
    RecompLauncherCGameInfo game={0};
    RecompLauncherCSettings settings={0};
    LauncherModel model;
    game.name="View test"; game.netplay_supported=1; game.netplay=&callbacks;
    game.netplay_view_labels=views; game.num_netplay_view_labels=5;
    settings.netplay_view_index=4;
    settings.adaptive_view=1;
    game.adaptive_view_supported=1;
    launcher_model_init(&model,&settings,&game,"");
    CHECK(model.num_netplay_view_labels==5 && model.s.netplay_view_index==4);
    CHECK(model.s.adaptive_view==1);
    // An old saved adaptive choice becomes native when the game withdraws
    // authorization, without changing the single-player adaptive preference.
    game.num_netplay_view_labels=4;
    launcher_model_init(&model,&settings,&game,"");
    CHECK(model.num_netplay_view_labels==4 && model.s.netplay_view_index==0);
    CHECK(model.s.adaptive_view==1);
    game.netplay_supported=0;
    launcher_model_init(&model,&settings,&game,"");
    CHECK(model.num_netplay_view_labels==0 && model.s.netplay_view_index==0);
    return 0;
}
