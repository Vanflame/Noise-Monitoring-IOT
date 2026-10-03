import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Hush — IoT classroom noise monitor with a traffic-light indicator",
  description: "An ESP32 listens, estimates classroom noise in real time and shows it on a traffic light. Privacy-first, offline-first, cloud-synced.",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <head>
        <link rel="preconnect" href="https://fonts.googleapis.com" />
        <link rel="preconnect" href="https://fonts.gstatic.com" crossOrigin="" />
        <link href="https://fonts.googleapis.com/css2?family=Fredoka:wght@500;600;700&family=Nunito:wght@400;600;700&display=swap" rel="stylesheet" />
      </head>
      <body>{children}</body>
    </html>
  );
}
