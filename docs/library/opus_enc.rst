:mod:`opus_enc` --- Opus audio encoder
=======================================

.. module:: opus_enc
   :synopsis: Opus audio encoder for ESP32-S3

This module exposes the `libopus <https://opus-codec.org/>`_ encoder as a
MicroPython C extension built with ``USER_C_MODULES``.  It is optimised for
real-time operation on ESP32-S3 with an INMP441 MEMS microphone.

.. note::

   Only one :class:`OpusEncoder` instance may be encoding at a time.  The
   library is compiled with ``NONTHREADSAFE_PSEUDOSTACK``; a single 60 KB
   global stack is shared across all instances.

Module constants
----------------

Application modes (pass to the constructor):

.. data:: VOIP

   Optimised for voice.  Uses the SILK codec with voice-activity detection.
   **Recommended for microphone capture.**

.. data:: AUDIO

   Optimised for general audio.  Uses the CELT codec.

.. data:: RESTRICTED_LOWDELAY

   Lowest possible latency.  CELT only, no LPC look-ahead.

Bitrate sentinels (pass to :meth:`~OpusEncoder.set_bitrate`):

.. data:: BITRATE_AUTO

   Let the encoder choose bitrate automatically.  Equivalent to ``-1000``.

.. data:: BITRATE_MAX

   Unconstrained bitrate (largest packets the encoder can produce).
   Equivalent to ``-1``.

class OpusEncoder
-----------------

.. class:: OpusEncoder(sample_rate, channels, application=VOIP, frame_ms=40)

   Create an Opus encoder.

   :param int sample_rate: Audio sample rate in Hz.
                           Must be one of ``8000``, ``12000``, ``16000``,
                           ``24000``, or ``48000``.
   :param int channels: ``1`` for mono, ``2`` for stereo.
   :param int application: One of :data:`VOIP`, :data:`AUDIO`, or
                           :data:`RESTRICTED_LOWDELAY`.
                           Defaults to :data:`VOIP`.
   :param int frame_ms: Opus frame duration in milliseconds.
                        Must be ``10``, ``20``, ``40``, or ``60``.
                        Defaults to ``40``.  Larger values reduce how often
                        ``encode()`` must be called, lowering scheduling
                        pressure at the cost of higher latency.

   Raises :exc:`ValueError` for invalid parameters or :exc:`OSError` if the
   encoder cannot be allocated.

   Default settings applied at construction:

   * bitrate  = 16 000 bps
   * complexity = 0

   Example::

      import opus_enc
      enc = opus_enc.OpusEncoder(16000, 1)                    # 40 ms frames (default)
      enc = opus_enc.OpusEncoder(16000, 1, opus_enc.VOIP, 20) # 20 ms frames

.. method:: OpusEncoder.encode(pcm_buf, out_buf) -> int

   Encode one frame of PCM audio.

   :param pcm_buf: Readable buffer of exactly :meth:`frame_bytes` bytes.
                   Data must be 16-bit signed PCM, little-endian, channel-
                   interleaved for stereo.
   :param out_buf: Writable buffer.  256 bytes or more is recommended
                   (maximum Opus packet is roughly 160 bytes at 16 kbps).
   :returns: Total bytes written to *out_buf*.

   The output layout is::

      out_buf[0:2]    uint16 little-endian  packet length N
      out_buf[2:2+N]  raw Opus packet bytes

   Raises :exc:`ValueError` if either buffer has the wrong size, or
   :exc:`OSError` on encoder failure.

   Example::

      FRAME_BYTES = enc.frame_bytes()   # e.g. 1280 at 16 kHz mono 40 ms
      pcm = bytearray(FRAME_BYTES)
      pkt = bytearray(256)

      n = 0
      while n < FRAME_BYTES:
          n += mic.readinto(memoryview(pcm)[n:])

      nbytes = enc.encode(pcm, pkt)
      # pkt[2:nbytes] is the raw Opus packet

.. method:: OpusEncoder.set_bitrate(bps)

   Set the target encode bitrate.

   :param int bps: Target bitrate in bits per second, or one of the
                   :data:`BITRATE_AUTO` / :data:`BITRATE_MAX` sentinels.

   Typical values for voice::

      enc.set_bitrate(16000)   # 16 kbps — clear voice, small packets
      enc.set_bitrate(24000)   # 24 kbps — better quality

   Default at construction: ``16000``.

.. method:: OpusEncoder.set_complexity(level)

   Set the encoder CPU/quality trade-off.

   :param int level: Integer 0–10.

   .. list-table::
      :header-rows: 1
      :widths: 10 30 60

      * - Level
        - CPU use
        - Notes
      * - **0** *(default)*
        - Minimum
        - Safe for all real-time workloads on ESP32-S3.
      * - 2–3
        - Low
        - Good quality; confirmed working real-time on ESP32-S3.
      * - 4–6
        - Medium
        - May cause buffer underruns at 16 kHz on ESP32-S3.
      * - 9
        - High
        - libopus library default; too slow for real-time on ESP32-S3.
      * - 10
        - Maximum
        - Not recommended for embedded use.

   Raises :exc:`ValueError` if *level* is outside 0–10.

   Default at construction: ``0``.

.. method:: OpusEncoder.set_dtx(enable)

   Enable or disable Discontinuous Transmission (silence suppression).

   :param bool enable: ``True`` to generate tiny comfort-noise packets (~2–5
                       bytes) during silence; ``False`` for full-sized packets
                       every frame (default).

   Useful for storage or network streaming to reduce bandwidth during silence.
   Disable during diagnostics so all frames are comparable.

.. method:: OpusEncoder.set_inband_fec(enable)

   Enable or disable in-band Forward Error Correction.

   :param bool enable: ``True`` to embed redundant data so a decoder can
                       recover from one lost packet (~50 % larger packets).

   Only useful for lossy transport (UDP, WiFi).  Has no benefit for file
   storage.  Only active in :data:`VOIP` application mode.

.. method:: OpusEncoder.frame_size() -> int

   Return the number of PCM samples per :meth:`encode` call.

   Equal to ``sample_rate * frame_ms // 1000``.

   .. list-table::
      :header-rows: 1

      * - Sample rate
        - 20 ms
        - 40 ms *(default)*
      * - 8 000 Hz
        - 160
        - 320
      * - 12 000 Hz
        - 240
        - 480
      * - **16 000 Hz**
        - **320**
        - **640**
      * - 24 000 Hz
        - 480
        - 960
      * - 48 000 Hz
        - 960
        - 1920

.. method:: OpusEncoder.frame_bytes() -> int

   Return ``frame_size() * channels * 2`` — the exact byte length that
   *pcm_buf* must be in each call to :meth:`encode`.

   Examples: ``640`` for 16 kHz mono 20 ms; ``1280`` for 16 kHz mono 40 ms (default).

.. method:: OpusEncoder.__del__()

   Free the underlying libopus encoder state.  Called automatically by the
   garbage collector.  Safe to call multiple times.  After calling, any
   subsequent :meth:`encode` raises :exc:`OSError` (``EBADF``).

Output packet format
--------------------

Every :meth:`~OpusEncoder.encode` call writes to *out_buf* as:

.. code-block:: text

   [ 2 bytes: uint16 LE length N ][ N bytes: raw Opus packet ]

To write to an Ogg Opus container, strip the 2-byte prefix::

   opus_packet = memoryview(pkt)[2:nbytes]

To write to a raw file for later conversion with ``raw2ogg.py``::

   f.write(memoryview(pkt)[:nbytes])   # raw2ogg.py expects the prefix

Recommended settings
--------------------

.. list-table::
   :header-rows: 1

   * - Use-case
     - ``sample_rate``
     - ``set_bitrate``
     - ``set_complexity``
     - ``set_dtx``
     - ``set_inband_fec``
   * - Voice recording to file
     - 16000
     - 16000
     - 0–3
     - True
     - False
   * - Voice over UDP/WiFi
     - 16000
     - 16000
     - 0–3
     - True
     - True
   * - High-quality audio
     - 48000
     - 64000
     - 5
     - False
     - False
   * - Minimum file size
     - 8000
     - 8000
     - 0
     - True
     - False
