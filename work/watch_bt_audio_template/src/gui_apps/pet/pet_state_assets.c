#include "pet_state_assets.h"

#include <stddef.h>

static const PET_STATE_ASSET l_aStateAssets[PET_BEHAVIOR_VISUAL_COUNT] =
{
    {PET_BEHAVIOR_VISUAL_CALM,        "/:/pet/calm.gif"},
    {PET_BEHAVIOR_VISUAL_HAPPY,       "/:/pet/happy.gif"},
    {PET_BEHAVIOR_VISUAL_CURIOUS,     "/:/pet/curious.gif"},
    {PET_BEHAVIOR_VISUAL_SLEEPY,      "/:/pet/sleepy.gif"},
    {PET_BEHAVIOR_VISUAL_LONELY,      "/:/pet/lonely.gif"},
    {PET_BEHAVIOR_VISUAL_CELEBRATE,   "/:/pet/celebrate.gif"},
    {PET_BEHAVIOR_VISUAL_STARTLED,    "/:/pet/startled.gif"},
    {PET_BEHAVIOR_VISUAL_ANNOYED,     "/:/pet/annoyed.gif"},
    {PET_BEHAVIOR_VISUAL_WORKING,     "/:/pet/working.gif"},
    {PET_BEHAVIOR_VISUAL_NEEDS_INPUT, "/:/pet/needs_input.gif"},
    {PET_BEHAVIOR_VISUAL_ERROR,       "/:/pet/error.gif"}
};

/* Return the versioned optional GIF mapping for a validated visual state. */
const PET_STATE_ASSET *PETSTATEASSET_Get(PET_BEHAVIOR_VISUAL_STATE eState)
{
    if (PET_BEHAVIOR_VISUAL_COUNT <= eState)
    {
        return NULL;
    }

    return &l_aStateAssets[eState];
}

/* Return the compile-time number of visual state entries. */
uint8_t PETSTATEASSET_Count(void)
{
    return (uint8_t)PET_BEHAVIOR_VISUAL_COUNT;
}
