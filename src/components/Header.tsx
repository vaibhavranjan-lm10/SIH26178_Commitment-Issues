import React, { useEffect, useState } from 'react';
import { 
  ShieldAlert, 
  Flame, 
  Waves, 
  Activity, 
  Wind, 
  Mountain, 
  BookOpen, 
  Bell, 
  Volume2, 
  VolumeX, 
  Radio, 
  Clock, 
  Layers, 
  Sparkles,
  Send
} from 'lucide-react';
import { DisasterCategory } from '../types/disaster';

interface HeaderProps {
  activeCategory: DisasterCategory;
  onSelectCategory: (category: DisasterCategory) => void;
  categoryCounts: Record<string, number>;
  totalAlerts: number;
  criticalCount: number;
  isSirenActive: boolean;
  onToggleSiren: () => void;
  onOpenBroadcast: () => void;
  onOpenLiveAlertsList: () => void;
}

const navItems: { label: DisasterCategory; icon: React.ComponentType<{ className?: string }> }[] = [
  { label: 'All', icon: Layers },
  { label: 'Floods', icon: Waves },
  { label: 'Forest Fires', icon: Flame },
  { label: 'Earthquakes', icon: Activity },
  { label: 'Cyclones', icon: Wind },
  { label: 'Landslides', icon: Mountain },
  { label: 'Resources', icon: BookOpen },
];

export const Header: React.FC<HeaderProps> = ({
  activeCategory,
  onSelectCategory,
  categoryCounts,
  totalAlerts,
  criticalCount,
  isSirenActive,
  onToggleSiren,
  onOpenBroadcast,
  onOpenLiveAlertsList,
}) => {
  const [timeString, setTimeString] = useState<string>('');

  useEffect(() => {
    const updateTime = () => {
      const now = new Date();
      setTimeString(
        now.toLocaleTimeString('en-IN', {
          timeZone: 'Asia/Kolkata',
          hour: '2-digit',
          minute: '2-digit',
          second: '2-digit',
          hour12: false,
        }) + ' IST'
      );
    };
    updateTime();
    const interval = setInterval(updateTime, 1000);
    return () => clearInterval(interval);
  }, []);

  return (
    <header className="h-16 border-b border-slate-800/80 bg-[#070a13]/95 backdrop-blur-xl px-4 lg:px-6 flex items-center justify-between z-30 shrink-0 select-none">
      {/* Left: Logo & National Grid Branding */}
      <div className="flex items-center gap-3.5">
        <div className="relative flex items-center justify-center w-10 h-10 rounded-xl bg-gradient-to-br from-red-500/20 via-orange-500/10 to-transparent border border-red-500/40 shadow-lg shadow-red-500/20">
          <ShieldAlert className="w-5 h-5 text-red-400 animate-pulse" />
          <span className="absolute -top-1 -right-1 flex h-3 w-3">
            <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-red-400 opacity-75"></span>
            <span className="relative inline-flex rounded-full h-3 w-3 bg-red-500"></span>
          </span>
        </div>

        <div>
          <div className="flex items-center gap-2">
            <h1 className="text-base sm:text-lg font-black tracking-wider text-slate-100 uppercase">
              INDIA DISASTER MONITOR <span className="text-red-500 font-extrabold">|</span> <span className="bg-gradient-to-r from-red-400 via-orange-400 to-amber-300 bg-clip-text text-transparent font-black">SIH 2026</span>
            </h1>
            <span className="hidden sm:inline-flex items-center px-1.5 py-0.5 rounded text-[10px] font-semibold bg-red-500/20 text-red-300 border border-red-500/30">
              NDMA GRID 2.0
            </span>
          </div>
          <p className="text-[11px] text-slate-400 font-medium flex items-center gap-2">
            <span className="inline-block w-2 h-2 rounded-full bg-emerald-500 animate-pulse"></span>
            National Early Warning & Emergency Response System
          </p>
        </div>
      </div>

      {/* Center Navigation Bar */}
      <nav className="hidden xl:flex items-center gap-1 bg-slate-900/70 p-1 rounded-xl border border-slate-800/80 shadow-inner">
        {navItems.map((item) => {
          const Icon = item.icon;
          const isActive = activeCategory === item.label;
          const count = item.label === 'All' ? totalAlerts : categoryCounts[item.label] || 0;

          return (
            <button
              key={item.label}
              onClick={() => onSelectCategory(item.label)}
              className={`flex items-center gap-2 px-3 py-1.5 rounded-lg text-xs font-semibold transition-all duration-200 ${
                isActive
                  ? 'bg-gradient-to-r from-red-500/20 to-orange-500/20 text-white border border-red-500/40 shadow-sm shadow-red-500/20'
                  : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/50 border border-transparent'
              }`}
            >
              <Icon className={`w-3.5 h-3.5 ${isActive ? 'text-red-400' : 'text-slate-400'}`} />
              <span>{item.label}</span>
              {count > 0 && (
                <span
                  className={`px-1.5 py-0.2 rounded-full text-[10px] font-bold ${
                    isActive
                      ? 'bg-red-500 text-white'
                      : 'bg-slate-800 text-slate-300'
                  }`}
                >
                  {count}
                </span>
              )}
            </button>
          );
        })}
      </nav>

      {/* Right Controls: Live Alerts Pill, Clock & Siren */}
      <div className="flex items-center gap-2.5">
        {/* Real-time Clock */}
        <div className="hidden lg:flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-slate-900/80 border border-slate-800 text-xs font-mono text-cyan-300">
          <Clock className="w-3.5 h-3.5 text-cyan-400" />
          <span>{timeString || 'LIVE IST'}</span>
        </div>

        {/* Siren Alert Toggle */}
        <button
          onClick={onToggleSiren}
          title={isSirenActive ? 'Mute Alert Siren' : 'Enable Audible Siren Alerts'}
          className={`p-2 rounded-lg border transition-all ${
            isSirenActive
              ? 'bg-red-500/20 border-red-500/50 text-red-400 shadow-lg shadow-red-500/20 animate-pulse'
              : 'bg-slate-900/80 border-slate-800 text-slate-400 hover:text-slate-200 hover:bg-slate-800'
          }`}
        >
          {isSirenActive ? <Volume2 className="w-4 h-4" /> : <VolumeX className="w-4 h-4" />}
        </button>

        {/* Broadcast CAP Alert */}
        <button
          onClick={onOpenBroadcast}
          className="hidden md:flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-gradient-to-r from-cyan-600/20 to-blue-600/20 border border-cyan-500/30 text-cyan-300 hover:text-cyan-100 hover:border-cyan-400 text-xs font-semibold shadow-sm transition-all"
        >
          <Send className="w-3.5 h-3.5 text-cyan-400" />
          <span>Broadcast SMS</span>
        </button>

        {/* Live Alerts Glassmorphic Pill */}
        <button
          onClick={onOpenLiveAlertsList}
          className="relative group flex items-center gap-2.5 px-3.5 py-1.5 rounded-full bg-gradient-to-r from-red-950/60 via-slate-900/80 to-slate-900/80 border border-red-500/40 text-red-200 hover:text-white hover:border-red-400 backdrop-blur-md shadow-lg shadow-red-950/40 transition-all duration-200"
        >
          <span className="flex h-2.5 w-2.5 relative">
            <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-red-400 opacity-75"></span>
            <span className="relative inline-flex rounded-full h-2.5 w-2.5 bg-red-500"></span>
          </span>
          <span className="text-xs font-bold tracking-wide">LIVE ALERTS</span>
          <span className="px-2 py-0.5 rounded-full text-[10px] font-black bg-red-500 text-white shadow-sm shadow-red-500/50">
            {totalAlerts}
          </span>
        </button>
      </div>
    </header>
  );
};
