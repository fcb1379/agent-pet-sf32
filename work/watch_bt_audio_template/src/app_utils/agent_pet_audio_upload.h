#ifndef AGENT_PET_AUDIO_UPLOAD_H
#define AGENT_PET_AUDIO_UPLOAD_H

#include <stdbool.h>

void AGENTPETAUDIO_Init(void);
void AGENTPETAUDIO_SetSubscribed(bool bSubscribed);
bool AGENTPETAUDIO_RequestStream(bool bStart);

#endif /* AGENT_PET_AUDIO_UPLOAD_H */
