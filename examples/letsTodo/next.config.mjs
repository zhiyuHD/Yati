/** @type {import('next').NextConfig} */
const nextConfig = {
  // 静态导出：产物是纯 HTML/JS/CSS，由 Yati 的 app:// 协议直接提供，不需要 Node 服务器。
  output: 'export',
  images: { unoptimized: true },
};

export default nextConfig;
