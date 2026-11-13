import './globals.css';

export const metadata = {
  title: 'LetsTodo',
  description: 'A tiny todo app built with Next.js and running on the Yati runtime',
};

export default function RootLayout({ children }) {
  return (
    <html lang="zh-CN">
      <body>{children}</body>
    </html>
  );
}
