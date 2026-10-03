import ContactForm from "./ContactForm";
import Meter from "./Meter";
import Icon from "./Icon";

const REPO = "https://github.com/Vanflame/Noise-Monitoring-IOT";

const FEATURES = [
  ["light", "Traffic-light feedback", "Green, yellow and red. Students know exactly where they stand without a teacher raising their voice."],
  ["timer", "Smart escalation", "Smoothing, hysteresis and timed warnings mean a dropped book never sets it off."],
  ["lock", "Privacy first", "No live listening and no continuous recording. Only a 5-second clip on major events."],
  ["offline", "Offline first", "Events and readings buffer to the SD card and sync when Wi-Fi is back."],
  ["tools", "On-device admin", "Wi-Fi setup, thresholds, timers, LED brightness and diagnostics from any phone."],
  ["cloud", "Cloud dashboard", "Incidents and clips sync to the cloud with role-based access for staff."],
];

const STEPS = [
  ["Listen", "An I2S digital microphone samples the room continuously."],
  ["Measure", "The ESP32 computes an estimated sound level and smooths it."],
  ["Signal", "The traffic light changes; warnings escalate while it stays red."],
  ["Report", "Incidents are logged with time and severity, then synced."],
];

const HARDWARE = ["ESP32", "INMP441 I2S mic", "RGB traffic light", "MicroSD card", "RTC module", "Speaker + MP3"];

export default function Page() {
  return (
    <main>
      <nav className="nav wrap">
        <a className="brand" href="#"><span className="brand__dots"><i /><i /><i /></span>hush</a>
        <div className="nav__links"><a href="#how">How it works</a><a href="#privacy">Privacy</a><a href="#contact">Contact</a><a className="btn btn--sm" href={REPO}>Source code</a></div>
      </nav>

      <header className="hero wrap">
        <div>
          <p className="pill">IoT · ESP32 · Capstone project</p>
          <h1>A calmer classroom, <span>one light</span> at a time.</h1>
          <p className="lede">Hush listens to the room, estimates the noise level in real time and shows it on a simple traffic light, so students can see when it&apos;s time to bring it down.</p>
          <div className="cta"><a className="btn" href="#how">See how it works</a><a className="btn btn--ghost" href={REPO}>View on GitHub</a></div>
        </div>
        <Meter />
      </header>

      <section className="sec wrap">
        <h2 className="center">Everything a classroom needs.<br /><span>Nothing it doesn&apos;t.</span></h2>
        <div className="grid">
          {FEATURES.map(([icon, t, d]) => <article key={t} className="card"><span className="card__icon"><Icon name={icon} /></span><h3>{t}</h3><p>{d}</p></article>)}
        </div>
      </section>

      <section id="how" className="sec sec--tint">
        <div className="wrap">
          <h2 className="center">How it works</h2>
          <ol className="steps">{STEPS.map(([t, d], i) => <li key={t}><span className="steps__n">{i + 1}</span><h3>{t}</h3><p>{d}</p></li>)}</ol>
        </div>
      </section>

      <section id="privacy" className="sec wrap split">
        <div>
          <h2>Built to <span>respect</span> students.</h2>
          <p className="lede">The device never streams audio and nobody can listen in. It only measures loudness. When noise stays at the major level, it saves exactly five seconds as evidence, and nothing more.</p>
          <ul className="checks"><li>No live listening</li><li>No continuous recording</li><li>Event-triggered 5-second clips only</li><li>Role-based access to recordings</li></ul>
        </div>
        <div className="hw">
          <h3>Inside the box</h3>
          <ul>{HARDWARE.map((h) => <li key={h}>{h}</li>)}</ul>
        </div>
      </section>

      <section className="sec wrap final">
        <h2>Ready to give it a try?</h2>
        <p className="lede center">Firmware, web UI and setup notes are all in the repository.</p>
        <a className="btn" href={REPO}>Get the source</a>
      </section>

      <ContactForm source="Hush (noise monitor)" title="Bring Hush to your school" lede="Interested in deploying it in your classrooms, or want a custom IoT device built? Let’s talk." interests={["Deploy Hush in our school", "Custom IoT device", "Research / capstone help", "Something else"]} />

      <footer className="foot wrap"><span>hush · IoT classroom noise monitoring</span><span>Built by Vanflame</span></footer>
    </main>
  );
}
