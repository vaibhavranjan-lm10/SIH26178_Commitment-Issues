import React from 'react';
import { Radio, Satellite, Server, ShieldCheck, Activity } from 'lucide-react';
import { mockTelemetryFeeds } from '../data/mockDisasters';

export const StatusBar: React.FC = () => {
  return (
    <footer className="h-8 border-t border-slate-800/90 bg-[#060810] px-4 flex items-center justify-between text-[11px] font-mono text-slate-400 z-30 shrink-0 select-none overflow-hidden">
      {/* Left status badge */}
      <div className="flex items-center gap-3 shrink-0 pr-4 border-r border-slate-800">
        <div className="flex items-center gap-1.5 text-emerald-400 font-bold">
          <span className="w-2 h-2 rounded-full bg-emerald-500 animate-ping"></span>
          <span>GRID 2.0 ONLINE</span>
        </div>
        <div className="hidden sm:flex items-center gap-1.5 text-slate-400">
          <Satellite className="w-3 h-3 text-cyan-400" />
          <span>INSAT-3DR / CARTOSAT-3</span>
        </div>
      </div>

      {/* Center scrolling ticker */}
      <div className="flex-1 overflow-hidden relative mx-4">
        <div className="flex items-center gap-8 whitespace-nowrap animate-marquee">
          {mockTelemetryFeeds.concat(mockTelemetryFeeds).map((feed, idx) => (
            <div key={idx} className="flex items-center gap-2">
              <span className="px-1.5 py-0.2 rounded bg-slate-800 text-cyan-300 font-black text-[10px] border border-slate-700">
                {feed.source}
              </span>
              <span className="text-slate-300">{feed.text}</span>
              <span className="text-slate-600 font-bold">&bull;</span>
            </div>
          ))}
        </div>
      </div>

      {/* Right status info */}
      <div className="flex items-center gap-3 shrink-0 pl-4 border-l border-slate-800 text-[10px]">
        <div className="hidden md:flex items-center gap-1 text-slate-400">
          <Server className="w-3 h-3 text-slate-500" />
          <span>LATENCY: <strong className="text-emerald-400">18ms</strong></span>
        </div>
        <span className="text-slate-500 font-bold">SIH 2026</span>
      </div>
    </footer>
  );
};
