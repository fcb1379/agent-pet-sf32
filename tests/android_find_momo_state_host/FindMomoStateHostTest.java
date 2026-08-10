package com.huangshan.badge;

final class FindMomoStateHostTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        FindMomoState state = new FindMomoState();

        state.applyDeviceState("time=20260811T120000;tz=480;img=0");
        check(!state.isAvailable(), "old firmware must remain hidden");
        state.applyDeviceState("time=20260811T120000;tz=480;img=0;find=1");
        check(state.isAvailable(), "find capability must be enabled");
        check(state.beginStart(65535), "first tap must start");
        check(!state.beginStart(1), "pending tap must be debounced");
        state.applyFindResponse("r=ACCEPTED;s=ACTIVE;id=65535;left=60;a=0");
        check(state.isActive(), "active response must restore state");
        check(!state.applyEndEvent(1, "TIMEOUT"), "stale END must be ignored");
        check(state.applyEndEvent(65535, "TIMEOUT"), "matching END must finish");
        check(!state.isActive(), "matching END must clear active state");

        state.applyFindResponse("r=STATUS;s=ACTIVE;id=1;left=20;a=0");
        check(state.isActive() && state.getSessionId() == 1,
                "reconnect STATUS must restore wrapped session id");
        check(state.beginStop(), "active session must allow stop");
        state.applyFindResponse("r=STOPPING;s=STOPPING;id=1;left=0;a=0");
        check(state.isActive() && state.isPending(), "STOPPING disables repeated stop until END");
        check(!state.beginStop(), "STOPPING must reject repeated stop taps");
        check(state.applyEndEvent(1, "PHONE_STOP"), "phone END must finish");

        System.out.println("FindMomoStateHostTest: PASS");
    }
}
