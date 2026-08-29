import React, { useState, useMemo, useEffect, useRef } from 'react';
import { Header } from './components/Header';
import { DisasterMap } from './components/DisasterMap';
import { LeftFeedPanel } from './components/LeftFeedPanel';
import { RightMetricsPanel } from './components/RightMetricsPanel';
import { IncidentDetailModal } from './components/IncidentDetailModal';
import { BroadcastModal } from './components/BroadcastModal';
import { LiveAlertsModal } from './components/LiveAlertsModal';
import { StatusBar } from './components/StatusBar';
import { 
  mockDisasterAlerts, 
  mockEmergencyContacts, 
  mockKeyMetrics, 
  mockTrendData 
} from './data/mockDisasters';
import { DisasterAlert, DisasterCategory } from './types/disaster';
import { Layers, List, BarChart3, Map, ShieldAlert } from 'lucide-react';

export const App: React.FC = () => {
  const [alerts, setAlerts] = useState<DisasterAlert[]>(mockDisasterAlerts);
  const [selectedCategory, setSelectedCategory] = useState<DisasterCategory>('All');
  const [selectedState, setSelectedState] = useState<string>('All States');
  const [selectedAlert, setSelectedAlert] = useState<DisasterAlert | null>(null);
  
  // Modals state
  const [activeModalAlert, setActiveModalAlert] = useState<DisasterAlert | null>(null);
  const [isBroadcastOpen, setIsBroadcastOpen] = useState<boolean>(false);
  const [isLiveAlertsOpen, setIsLiveAlertsOpen] = useState<boolean>(false);

  // Mobile layout tab switcher ('map' | 'feed' | 'metrics')
  const [mobileTab, setMobileTab] = useState<'map' | 'feed' | 'metrics'>('map');

  // Siren sound state
  const [isSirenActive, setIsSirenActive] = useState<boolean>(false);
  const audioCtxRef = useRef<AudioContext | null>(null);
  const sirenOscRef = useRef<OscillatorNode | null>(null);
  const sirenGainRef = useRef<GainNode | null>(null);

  // Toggle Web Audio API synthesized emergency siren
  const toggleSiren = () => {
    if (isSirenActive) {
      // Stop siren
      try {
        if (sirenOscRef.current) {
          sirenOscRef.current.stop();
          sirenOscRef.current.disconnect();
          sirenOscRef.current = null;
        }
      } catch (e) {
        console.error(e);
      }
      setIsSirenActive(false);
    } else {
      // Start siren
      try {
        const AudioContextClass = window.AudioContext || (window as any).webkitAudioContext;
        const ctx = audioCtxRef.current || new AudioContextClass();
        audioCtxRef.current = ctx;

        if (ctx.state === 'suspended') {
          ctx.resume();
        }

        const osc = ctx.createOscillator();
        const gain = ctx.createGain();

        osc.type = 'sawtooth';
        osc.frequency.setValueAtTime(600, ctx.currentTime);
        
        // Modulate frequency for warble effect
        const now = ctx.currentTime;
        osc.frequency.setValueCurveAtTime(
          new Float32Array([600, 900, 600, 900, 600, 900, 600]),
          now,
          3.0
        );

        gain.gain.setValueAtTime(0.08, ctx.currentTime);

        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start();

        sirenOscRef.current = osc;
        sirenGainRef.current = gain;
        setIsSirenActive(true);

        // Auto-turn off after 6 seconds
        setTimeout(() => {
          if (sirenOscRef.current) {
            try {
              sirenOscRef.current.stop();
              sirenOscRef.current.disconnect();
              sirenOscRef.current = null;
            } catch (err) {}
          }
          setIsSirenActive(false);
        }, 6000);
      } catch (e) {
        console.warn('AudioContext not allowed without user gesture:', e);
      }
    }
  };

  // Clean up audio on unmount
  useEffect(() => {
    return () => {
      if (sirenOscRef.current) {
        try {
          sirenOscRef.current.stop();
          sirenOscRef.current.disconnect();
        } catch (e) {}
      }
    };
  }, []);

  // Category counts
  const categoryCounts = useMemo(() => {
    const counts: Record<string, number> = {};
    alerts.forEach((a) => {
      counts[a.category] = (counts[a.category] || 0) + 1;
    });
    return counts;
  }, [alerts]);

  // Unique states
  const statesList = useMemo(() => {
    const set = new Set<string>();
    alerts.forEach((a) => set.add(a.state));
    return Array.from(set).sort();
  }, [alerts]);

  // Filtered alerts
  const filteredAlerts = useMemo(() => {
    return alerts.filter((alert) => {
      const matchCategory =
        selectedCategory === 'All' ||
        selectedCategory === 'Resources' ||
        alert.category === selectedCategory;

      const matchState =
        selectedState === 'All States' || alert.state === selectedState;

      return matchCategory && matchState;
    });
  }, [alerts, selectedCategory, selectedState]);

  const criticalCount = useMemo(() => {
    return alerts.filter((a) => a.severity === 'High Danger').length;
  }, [alerts]);

  return (
    <div className="h-screen w-screen flex flex-col bg-[#070a13] text-slate-100 overflow-hidden font-sans">
      {/* 1. Header */}
      <Header
        activeCategory={selectedCategory}
        onSelectCategory={(cat) => {
          setSelectedCategory(cat);
          setMobileTab('map');
        }}
        categoryCounts={categoryCounts}
        totalAlerts={alerts.length}
        criticalCount={criticalCount}
        isSirenActive={isSirenActive}
        onToggleSiren={toggleSiren}
        onOpenBroadcast={() => setIsBroadcastOpen(true)}
        onOpenLiveAlertsList={() => setIsLiveAlertsOpen(true)}
      />

      {/* 2. Mobile Tab Switcher (Visible only on smaller screens) */}
      <div className="flex md:hidden items-center justify-around bg-slate-900 border-b border-slate-800 p-1.5 z-20">
        <button
          onClick={() => setMobileTab('map')}
          className={`flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-bold ${
            mobileTab === 'map' ? 'bg-cyan-500 text-slate-950' : 'text-slate-400'
          }`}
        >
          <Map className="w-3.5 h-3.5" />
          <span>Map Radar</span>
        </button>
        <button
          onClick={() => setMobileTab('feed')}
          className={`flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-bold ${
            mobileTab === 'feed' ? 'bg-cyan-500 text-slate-950' : 'text-slate-400'
          }`}
        >
          <List className="w-3.5 h-3.5" />
          <span>Alerts Feed ({filteredAlerts.length})</span>
        </button>
        <button
          onClick={() => setMobileTab('metrics')}
          className={`flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-bold ${
            mobileTab === 'metrics' ? 'bg-cyan-500 text-slate-950' : 'text-slate-400'
          }`}
        >
          <BarChart3 className="w-3.5 h-3.5" />
          <span>Metrics & SAR</span>
        </button>
      </div>

      {/* 3. Main Mission Control Dashboard Workspace */}
      <div className="flex-1 flex relative overflow-hidden">
        {/* Left Side Alert Feed Panel */}
        <div className={`h-full ${mobileTab === 'feed' ? 'block w-full' : 'hidden md:block'}`}>
          <LeftFeedPanel
            alerts={filteredAlerts}
            selectedAlert={selectedAlert}
            onSelectAlert={(alert) => {
              setSelectedAlert(alert);
              setMobileTab('map'); // On mobile, automatically jump to map when selected
            }}
            onOpenDetails={(alert) => setActiveModalAlert(alert)}
          />
        </div>

        {/* Center Interactive Leaflet Map Container */}
        <div className={`flex-1 h-full relative ${mobileTab === 'map' ? 'block w-full' : 'hidden md:block'}`}>
          <DisasterMap
            alerts={filteredAlerts}
            selectedAlert={selectedAlert}
            onSelectAlert={(alert) => setSelectedAlert(alert)}
            onOpenDetails={(alert) => setActiveModalAlert(alert)}
          />
        </div>

        {/* Right Metrics & Emergency Response Panel */}
        <div className={`h-full ${mobileTab === 'metrics' ? 'block w-full' : 'hidden md:block'}`}>
          <RightMetricsPanel
            metrics={mockKeyMetrics}
            trendData={mockTrendData}
            contacts={mockEmergencyContacts}
            selectedCategory={selectedCategory}
            onSelectCategory={setSelectedCategory}
            selectedState={selectedState}
            onSelectState={setSelectedState}
            statesList={statesList}
            onOpenReportModal={() => setIsLiveAlertsOpen(true)}
          />
        </div>
      </div>

      {/* 4. Telemetry Bottom Status Bar */}
      <StatusBar />

      {/* 5. Modals */}
      {/* Situational Report (SitRep) Detail Modal */}
      <IncidentDetailModal
        alert={activeModalAlert}
        onClose={() => setActiveModalAlert(null)}
        onOpenBroadcast={() => {
          setActiveModalAlert(null);
          setIsBroadcastOpen(true);
        }}
      />

      {/* Emergency CAP Broadcast SMS Modal */}
      <BroadcastModal
        isOpen={isBroadcastOpen}
        onClose={() => setIsBroadcastOpen(false)}
        selectedAlert={selectedAlert}
      />

      {/* Live Alerts Full List Modal */}
      <LiveAlertsModal
        isOpen={isLiveAlertsOpen}
        onClose={() => setIsLiveAlertsOpen(false)}
        alerts={alerts}
        onSelectAlert={(alert) => {
          setSelectedAlert(alert);
          setMobileTab('map');
        }}
        onOpenDetails={(alert) => setActiveModalAlert(alert)}
      />
    </div>
  );
};
