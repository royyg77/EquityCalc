import { useEffect, useState } from 'react';

export default function App() {
  const [status, setStatus] = useState<string>('checking...');

  useEffect(() => {
    fetch('/health')
      .then((r) => r.json())
      .then((data: { status: string }) => setStatus(data.status))
      .catch(() => setStatus('backend unreachable'));
  }, []);

  return (
    <div style={{ fontFamily: 'system-ui', padding: '2rem' }}>
      <h1>EquityCalc</h1>
      <p>Backend status: <strong>{status}</strong></p>
    </div>
  );
}
