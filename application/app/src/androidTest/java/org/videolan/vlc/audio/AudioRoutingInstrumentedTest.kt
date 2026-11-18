package org.videolan.vlc.audio

import android.net.Uri
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.videolan.libvlc.Media
import org.videolan.libvlc.MediaPlayer
import org.videolan.resources.VLCInstance

@RunWith(AndroidJUnit4::class)
class AudioRoutingInstrumentedTest {
    @Test
    fun switchesRoutesOnActivePlayer() {
        withPlayer("/sdcard/Music/vlccustom/stereo_distinct.wav") { context, player ->
            waitForPlayback(player)
            Thread.sleep(1_500L)

            assertTrue(AudioRouting.applyRoute(context, player, AudioRouting.Route.LEFT))
            assertEquals(AudioRouting.Route.LEFT, AudioRouting.getNativeRoute())
            waitForRenderedSamples()

            assertDirectionalOutput(AudioRouting.Route.LEFT, true)
            assertTrue(AudioRouting.applyRoute(context, player, AudioRouting.Route.RIGHT))
            assertEquals(AudioRouting.Route.RIGHT, AudioRouting.getNativeRoute())
            assertDirectionalOutput(AudioRouting.Route.RIGHT, false)

            assertTrue(AudioRouting.applyRoute(context, player, AudioRouting.Route.STEREO))
            assertEquals(AudioRouting.Route.STEREO, AudioRouting.getNativeRoute())
            AudioRouting.resetOutputMeterForTest()
            Thread.sleep(600L)
            assertTrue("Stereo output had no signal",
                    AudioRouting.getOutputFramesForTest() > 0L)
            assertTrue("Stereo left channel was silent",
                    AudioRouting.getOutputEnergyLeftForTest() > 0L)
            assertTrue("Stereo right channel was silent",
                    AudioRouting.getOutputEnergyRightForTest() > 0L)

            val playingTimeBeforePause = player.time
            player.pause()
            Thread.sleep(400L)
            val renderedAtPause = AudioRouting.getRenderedSamples()
            Thread.sleep(300L)
            assertEquals("PCM continued rendering while paused",
                    renderedAtPause, AudioRouting.getRenderedSamples())

            assertTrue(AudioRouting.applyRoute(context, player, AudioRouting.Route.LEFT))
            player.play()
            Thread.sleep(500L)
            assertTrue("PCM did not resume",
                    AudioRouting.getRenderedSamples() > renderedAtPause)
            assertTrue("Player time did not advance after resume",
                    player.time > playingTimeBeforePause)

            player.time = player.time + 3_000L
            val renderedBeforeSeekContinuation = AudioRouting.getRenderedSamples()
            Thread.sleep(500L)
            assertTrue("PCM did not continue after seek",
                    AudioRouting.getRenderedSamples() > renderedBeforeSeekContinuation)
            assertTrue("AudioTrack did not restart after seek",
                    AudioRouting.isTrackPlayingForTest(player))
            val playbackHeadAfterSeek = AudioRouting.getPlaybackHeadPositionForTest(player)
            assertTrue("AudioTrack playback head was unavailable after seek",
                    playbackHeadAfterSeek >= 0)
            Thread.sleep(600L)
            assertTrue("AudioTrack playback head did not advance after seek",
                    AudioRouting.getPlaybackHeadPositionForTest(player) > playbackHeadAfterSeek)
            assertEquals(AudioRouting.Route.LEFT, AudioRouting.getNativeRoute())

            repeat(20) { index ->
                val route = when (index % 3) {
                    0 -> AudioRouting.Route.RIGHT
                    1 -> AudioRouting.Route.STEREO
                    else -> AudioRouting.Route.LEFT
                }
                assertTrue(AudioRouting.applyRoute(context, player, route))
            }
            AudioRouting.applyRoute(context, player, AudioRouting.Route.LEFT)
            Thread.sleep(300L)
            assertEquals(AudioRouting.Route.LEFT, AudioRouting.getNativeRoute())
        }
    }

    @Test
    fun resumesAudioAfterStopAndReplay() {
        withPlayer("/sdcard/Music/vlccustom/stereo_distinct.wav") { context, player ->
            assertTrue(AudioRouting.applyRoute(context, player, AudioRouting.Route.STEREO))
            waitForPlayback(player)
            Thread.sleep(1_000L)
            player.stop()
            Thread.sleep(300L)

            assertTrue(AudioRouting.preparePlayer(player))
            player.play()
            waitForPlayback(player)
            Thread.sleep(500L)
            assertTrue("AudioTrack was not playing after replay",
                    AudioRouting.isTrackPlayingForTest(player))
            val before = AudioRouting.getPlaybackHeadPositionForTest(player)
            assertTrue("AudioTrack playback head was unavailable after replay", before >= 0)
            Thread.sleep(600L)
            assertTrue("AudioTrack playback head did not advance after replay",
                    AudioRouting.getPlaybackHeadPositionForTest(player) > before)
            assertTrue("PCM was not rendered after replay",
                    AudioRouting.getRenderedSamples() > 0L)
            assertEquals(AudioRouting.Route.STEREO, AudioRouting.getNativeRoute())
        }
    }

    @Test
    fun handlesCommonCodecsMonoAndSurround() {
        val files = listOf(
                "mono_660.wav",
                "stereo_distinct.flac",
                "stereo_distinct.mp3",
                "stereo_distinct.opus",
                "stereo_aac.m4a",
                "surround_5_1.wav",
                "surround_7_1.wav"
        )
        files.forEach { file ->
            withPlayer("/sdcard/Music/vlccustom/$file") { context, player ->
                waitForPlayback(player)
                assertTrue("Could not select left route for $file",
                        AudioRouting.applyRoute(
                                context,
                                player,
                                AudioRouting.Route.LEFT
                        ))
                assertDirectionalOutput(AudioRouting.Route.LEFT, true)
            }
        }
    }

    @Test
    fun keepsRouteAcrossAudioTrackSwitch() {
        withPlayer("/sdcard/Music/vlccustom/dual_audio.mka") { context, player ->
            waitForPlayback(player)
            val trackDeadline = System.currentTimeMillis() + 5_000L
            while (player.audioTracksCount < 2 && System.currentTimeMillis() < trackDeadline) {
                Thread.sleep(100L)
            }
            assertTrue("Dual-track media did not expose two audio tracks",
                    player.audioTracksCount >= 2)
            assertTrue(AudioRouting.applyRoute(context, player, AudioRouting.Route.RIGHT))

            assertTrue(player.setAudioTrack(0))
            val beforeFirstSwitch = AudioRouting.getRenderedSamples()
            Thread.sleep(500L)
            assertTrue(AudioRouting.getRenderedSamples() > beforeFirstSwitch)
            assertEquals(AudioRouting.Route.RIGHT, AudioRouting.getNativeRoute())

            assertTrue(player.setAudioTrack(1))
            val beforeSecondSwitch = AudioRouting.getRenderedSamples()
            Thread.sleep(500L)
            assertTrue(AudioRouting.getRenderedSamples() > beforeSecondSwitch)
            assertDirectionalOutput(AudioRouting.Route.RIGHT, false)
        }
    }

    @Test
    fun persistsRoute() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        try {
            AudioRouting.saveRoute(context, AudioRouting.Route.LEFT)
            assertEquals(AudioRouting.Route.LEFT, AudioRouting.getSavedRoute(context))
            AudioRouting.restore(context)
            assertEquals(AudioRouting.Route.LEFT, AudioRouting.getNativeRoute())
            AudioRouting.saveRoute(context, AudioRouting.Route.RIGHT)
            assertEquals(AudioRouting.Route.RIGHT, AudioRouting.getSavedRoute(context))
        } finally {
            AudioRouting.saveRoute(context, AudioRouting.Route.STEREO)
            AudioRouting.restore(context)
        }
    }

    @Test
    fun emitsRoutingSequenceForOutputCapture() {
        withPlayer("/sdcard/Music/vlccustom/stereo_distinct.wav") { context, player ->
            waitForPlayback(player)
            listOf(
                    AudioRouting.Route.STEREO to 3_000L,
                    AudioRouting.Route.LEFT to 3_000L,
                    AudioRouting.Route.RIGHT to 3_000L,
                    AudioRouting.Route.STEREO to 1_500L
            ).forEach { (route, duration) ->
                assertTrue(AudioRouting.applyRoute(context, player, route))
                Log.i(
                        "VLC/AudioRoutingCapture",
                        "CAPTURE_PHASE=${route.name} durationMs=$duration"
                )
                Thread.sleep(duration)
            }
        }
    }

    private fun withPlayer(
            path: String,
            block: (android.content.Context, MediaPlayer) -> Unit
    ) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val libVlc = VLCInstance.getInstance(context)
        val media = Media(libVlc, Uri.parse("file://$path"))
        val player = MediaPlayer(libVlc)
        try {
            assertTrue(AudioRouting.preparePlayer(player))
            player.media = media
            player.play()
            block(context, player)
        } finally {
            player.stop()
            player.release()
            media.release()
        }
    }

    private fun waitForPlayback(player: MediaPlayer) {
        val deadline = System.currentTimeMillis() + 10_000L
        while (!player.isPlaying && System.currentTimeMillis() < deadline) {
            Thread.sleep(100L)
        }
        assertTrue("Test media did not start", player.isPlaying)
    }

    private fun waitForRenderedSamples() {
        val deadline = System.currentTimeMillis() + 10_000L
        while (AudioRouting.getRenderedSamples() == 0L
                && System.currentTimeMillis() < deadline) {
            Thread.sleep(100L)
        }
        assertTrue("Custom PCM callback did not render samples",
                AudioRouting.getRenderedSamples() > 0L)
    }

    private fun assertDirectionalOutput(route: AudioRouting.Route, leftTarget: Boolean) {
        assertEquals(
                route,
                AudioRouting.getNativeRoute()
        )
        Thread.sleep(500L)
        AudioRouting.resetOutputMeterForTest()
        Thread.sleep(600L)
        assertTrue("Directional output did not render",
                AudioRouting.getOutputFramesForTest() > 0L)
        val leftEnergy = AudioRouting.getOutputEnergyLeftForTest()
        val rightEnergy = AudioRouting.getOutputEnergyRightForTest()
        val targetEnergy = if (leftTarget) leftEnergy else rightEnergy
        val silentEnergy = if (leftTarget) rightEnergy else leftEnergy
        assertNotEquals("Target channel was silent", 0L, targetEnergy)
        assertEquals("Non-target channel was not silent", 0L, silentEnergy)
    }
}
