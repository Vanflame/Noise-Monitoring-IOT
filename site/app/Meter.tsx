"use client";
import { useEffect, useRef, useState } from "react";

const GREEN = 55, YELLOW = 70; // dB thresholds (configurable on the device)

export default function Meter() {
  const [db, setDb] = useState(48);
  const [redFor, setRedFor] = useState(0);
  const t = useRef(0);

  useEffect(() => {
    const id = setInterval(() => {
      t.current += 1;
      // a "class" that slowly gets louder, then gets reminded and calms down
      const phase = t.current % 60;
      const target = phase < 20 ? 50 : phase < 38 ? 50 + (phase - 20) * 1.6 : phase < 46 ? 80 : 46;
      setDb((d) => Math.round((d * 0.7 + (target + (Math.random() * 8 - 4)) * 0.3) * 10) / 10);
    }, 250);
    return () => clearInterval(id);
  }, []);

  const level = db < GREEN ? "green" : db < YELLOW ? "yellow" : "red";
  useEffect(() => { setRedFor((r) => (level === "red" ? r + 0.25 : 0)); }, [db, level]);
  const warning = redFor > 4 ? "Major warning · recording 5s clip" : redFor > 2 ? "Second warning" : redFor > 0.5 ? "First warning" : null;

  return (
    <div className="meter" aria-label={`Simulated noise level ${db} dB, ${level}`}>
      <div className="light">
        {(["red", "yellow", "green"] as const).map((c) => <span key={c} className={`lamp lamp--${c} ${level === c ? "is-on" : ""}`} />)}
      </div>
      <div className="readout">
        <p className="readout__label">Room 204 · live</p>
        <p className="readout__db"><b>{db.toFixed(0)}</b> dB</p>
        <div className="bar"><span style={{ width: `${Math.min(100, Math.max(0, (db - 30) * 1.6))}%` }} className={`bar__fill bar__fill--${level}`} /></div>
        <div className="scale"><span>quiet</span><span>moderate</span><span>too loud</span></div>
        <p className={`status status--${level}`}>{warning ?? (level === "green" ? "All good, keep it up!" : level === "yellow" ? "Getting a bit noisy…" : "Too loud!")}</p>
      </div>
    </div>
  );
}
