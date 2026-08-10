package com.huangshan.badge;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class FindMomoStateTest {
    @Test public void oldFirmwareRemainsUnavailable() {
        FindMomoState state = new FindMomoState();
        state.applyDeviceState("time=20260811T120000;tz=480;img=1");
        assertFalse(state.isAvailable());
        assertFalse(state.beginStart(1));
    }

    @Test public void capabilityEnablesStartAndDuplicateTapIsDebounced() {
        FindMomoState state = new FindMomoState();
        state.applyDeviceState("time=20260811T120000;tz=480;img=1;find=1");
        assertTrue(state.isAvailable());
        assertTrue(state.beginStart(42));
        assertFalse(state.beginStart(43));
        state.applyFindResponse("r=ACCEPTED;s=ACTIVE;id=42;left=60;a=0");
        assertTrue(state.isActive());
        assertEquals(42, state.getSessionId());
    }

    @Test public void reconnectStatusRestoresActiveSession() {
        FindMomoState state = new FindMomoState();
        state.applyDeviceState("time=20260811T120000;tz=480;img=0;find=1");
        state.applyFindResponse("r=STATUS;s=ACTIVE;id=65535;left=31;a=0");
        assertTrue(state.isActive());
        assertEquals(65535, state.getSessionId());
    }

    @Test public void staleEndEventDoesNotStopCurrentSession() {
        FindMomoState state = new FindMomoState();
        state.applyDeviceState("time=20260811T120000;tz=480;img=0;find=1");
        assertTrue(state.beginStart(10));
        state.applyFindResponse("r=ACCEPTED;s=ACTIVE;id=10;left=60;a=0");
        assertFalse(state.applyEndEvent(9, "TIMEOUT"));
        assertTrue(state.isActive());
        assertTrue(state.applyEndEvent(10, "USER_STOP"));
        assertFalse(state.isActive());
        assertEquals(0, state.getSessionId());
    }

    @Test public void busyResponseReturnsToIdle() {
        FindMomoState state = new FindMomoState();
        state.applyDeviceState("time=20260811T120000;tz=480;img=0;find=1");
        assertTrue(state.beginStart(77));
        state.applyFindResponse("r=BUSY_TRANSFER;s=IDLE;id=0;left=0;a=0");
        assertFalse(state.isActive());
        assertFalse(state.isPending());
        assertEquals(0, state.getSessionId());
    }

    @Test public void stoppingResponseKeepsPendingUntilEnd() {
        FindMomoState state = new FindMomoState();
        state.applyDeviceState("find=1");
        state.applyFindResponse("r=STATUS;s=ACTIVE;id=11;left=30;a=0");
        assertTrue(state.beginStop());
        state.applyFindResponse("r=STOPPING;s=STOPPING;id=11;left=0;a=0");
        assertTrue(state.isActive());
        assertTrue(state.isPending());
        assertFalse(state.beginStop());
        assertTrue(state.applyEndEvent(11, "PHONE_STOP"));
        assertFalse(state.isPending());
    }
}
