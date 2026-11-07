// Optional WebAudio passive-piezo chiptune synthesizer mimicking M5StickC Plus2 buzzer.
(function (root) {
  'use strict';

  function createBuzzer() {
    let ctx = null;
    let enabled = false;

    function ensureContext() {
      if (!enabled) return null;
      const AudioCtx = root.AudioContext || root.webkitAudioContext;
      if (!AudioCtx) return null;
      if (!ctx) {
        ctx = new AudioCtx();
      }
      if (ctx.state === 'suspended') {
        ctx.resume().catch(function () {});
      }
      return ctx;
    }

    function playNotes(notes) {
      const audio = ensureContext();
      if (!audio) return;
      let start = audio.currentTime;
      for (let i = 0; i < notes.length; i++) {
        const n = notes[i];
        if (n.freq > 0) {
          const osc = audio.createOscillator();
          const gain = audio.createGain();
          osc.type = n.wave || 'square';
          osc.frequency.setValueAtTime(n.freq, start);
          gain.gain.setValueAtTime(0.035, start);
          gain.gain.exponentialRampToValueAtTime(0.001, start + n.dur);
          osc.connect(gain);
          gain.connect(audio.destination);
          osc.start(start);
          osc.stop(start + n.dur);
        }
        start += n.dur + (n.gap || 0.01);
      }
    }

    return {
      isEnabled() {
        return enabled;
      },
      setEnabled(flag) {
        enabled = Boolean(flag);
        if (enabled) {
          ensureContext();
          playNotes([
            { freq: 880, dur: 0.04 },
            { freq: 1320, dur: 0.06 }
          ]);
        }
        return enabled;
      },
      toggle() {
        return this.setEnabled(!enabled);
      },
      clickA() {
        playNotes([{ freq: 1046, dur: 0.035 }]);
      },
      clickB() {
        playNotes([
          { freq: 784, dur: 0.03 },
          { freq: 1175, dur: 0.04 }
        ]);
      },
      themeSwap() {
        playNotes([
          { freq: 659, dur: 0.035 },
          { freq: 880, dur: 0.035 },
          { freq: 1318, dur: 0.055 }
        ]);
      },
      scanDone(apCount) {
        if (apCount === 0) {
          playNotes([
            { freq: 440, dur: 0.06, wave: 'triangle' },
            { freq: 330, dur: 0.09, wave: 'triangle' }
          ]);
        } else if (apCount >= 10) {
          playNotes([
            { freq: 784, dur: 0.04 },
            { freq: 988, dur: 0.04 },
            { freq: 1175, dur: 0.04 },
            { freq: 1568, dur: 0.08 }
          ]);
        } else {
          playNotes([
            { freq: 880, dur: 0.04, wave: 'triangle' },
            { freq: 1175, dur: 0.06, wave: 'triangle' }
          ]);
        }
      },
      boop() {
        playNotes([
          { freq: 988, dur: 0.04, wave: 'triangle' },
          { freq: 1480, dur: 0.06, wave: 'triangle' }
        ]);
      },
      pwn() {
        playNotes([
          { freq: 587, dur: 0.04 },
          { freq: 784, dur: 0.04 },
          { freq: 1175, dur: 0.05 },
          { freq: 1568, dur: 0.09 }
        ]);
      },
      peer() {
        playNotes([
          { freq: 1046, dur: 0.045, wave: 'triangle' },
          { freq: 1318, dur: 0.045, wave: 'triangle' },
          { freq: 1568, dur: 0.045, wave: 'triangle' },
          { freq: 2093, dur: 0.08, wave: 'triangle' }
        ]);
      }
    };
  }

  root.ZD = root.ZD || {};
  root.ZD.createBuzzer = createBuzzer;

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { createBuzzer };
  }
})(typeof globalThis !== 'undefined' ? globalThis : window);
