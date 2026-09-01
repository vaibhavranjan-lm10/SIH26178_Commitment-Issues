import React, { useState, useMemo } from 'react';
import { 
  Search, 
  Flame, 
  Waves, 
  Activity, 
  Wind, 
  Mountain, 
  AlertTriangle, 
  MapPin, 
  Clock, 
  Users, 
  Crosshair, 
  ChevronRight, 
  SlidersHorizontal,
  FileText,
  ShieldCheck,
  Radio
} from 'lucide-react';
import { DisasterAlert, SeverityLevel } from '../types/disaster';

interface LeftFeedPanelProps {
  alerts: DisasterAlert[];
  selectedAlert: DisasterAlert | null;
  onSelectAlert: (alert: DisasterAlert) => void;
  onOpenDetails: (alert: DisasterAlert) => void;
}

export const LeftFeedPanel: React.FC<LeftFeedPanelProps> = ({
  alerts,
  selectedAlert,
  onSelectAlert,
  onOpenDetails,
}) => {
  const [searchQuery, setSearchQuery] = useState<string>('');
  const [severityFilter, setSeverityFilter] = useState<'All' | SeverityLevel>('All');
  const [activeTab, setActiveTab] = useState<'recent' | 'events'>('recent');

  const filteredAlerts = useMemo(() => {
    return alerts.filter((alert) => {
      const matchesSearch = 
        alert.title.toLowerCase().includes(searchQuery.toLowerCase()) ||
        alert.location.toLowerCase().includes(searchQuery.toLowerCase()) ||
        alert.state.toLowerCase().includes(searchQuery.toLowerCase()) ||
        alert.category.toLowerCase().includes(searchQuery.toLowerCase());

      const matchesSeverity =
        severityFilter === 'All' || alert.severity === severityFilter;

      return matchesSearch && matchesSeverity;
    });
  }, [alerts, searchQuery, severityFilter]);

  const getCategoryIcon = (category: string) => {
    switch (category) {
      case 'Floods': return <Waves className="w-4 h-4 text-blue-400" />;
      case 'Forest Fires': return <Flame className="w-4 h-4 text-orange-400" />;
      case 'Earthquakes': return <Activity className="w-4 h-4 text-yellow-400" />;
      case 'Cyclones': return <Wind className="w-4 h-4 text-cyan-400" />;
      case 'Landslides': return <Mountain className="w-4 h-4 text-amber-400" />;
      default: return <AlertTriangle className="w-4 h-4 text-red-400" />;
    }
  };

  return (
    <aside className="w-full md:w-80 lg:w-[350px] xl:w-[370px] h-full flex flex-col bg-[#0b0f19]/90 border-r border-slate-800/80 backdrop-blur-xl shrink-0 z-20 select-none">
      {/* Panel Top Header */}
      <div className="p-3.5 border-b border-slate-800/80 space-y-3">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Radio className="w-4 h-4 text-red-500 animate-pulse" />
            <h2 className="text-xs font-black uppercase tracking-wider text-slate-200">
              Live Alert Feeds
            </h2>
          </div>
          <span className="text-[11px] font-mono px-2 py-0.5 rounded-full bg-slate-800 text-cyan-300 border border-slate-700 font-bold">
            {filteredAlerts.length} ACTIVE
          </span>
        </div>

        {/* Tab Selector: Recent Events vs Disaster Events */}
        <div className="grid grid-cols-2 gap-1 p-1 bg-slate-900/90 rounded-lg border border-slate-800">
          <button
            onClick={() => setActiveTab('recent')}
            className={`py-1.5 px-2 rounded-md text-xs font-bold transition-all ${
              activeTab === 'recent'
                ? 'bg-slate-800 text-white shadow-sm border border-slate-700'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Recent Events
          </button>
          <button
            onClick={() => setActiveTab('events')}
            className={`py-1.5 px-2 rounded-md text-xs font-bold transition-all ${
              activeTab === 'events'
                ? 'bg-slate-800 text-white shadow-sm border border-slate-700'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Disaster Events
          </button>
        </div>

        {/* Search Bar */}
        <div className="relative">
          <Search className="w-3.5 h-3.5 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Search state, district, or event..."
            className="w-full pl-9 pr-3 py-1.5 bg-slate-900/90 border border-slate-800 rounded-lg text-xs text-slate-200 placeholder-slate-500 focus:outline-none focus:border-cyan-500/50 transition-all font-sans"
          />
        </div>

        {/* Severity Quick Filters */}
        <div className="flex items-center gap-1.5 overflow-x-auto pb-0.5">
          <button
            onClick={() => setSeverityFilter('All')}
            className={`px-2.5 py-1 rounded-md text-[11px] font-bold shrink-0 transition-all ${
              severityFilter === 'All'
                ? 'bg-slate-700 text-white border border-slate-600'
                : 'bg-slate-900/60 text-slate-400 hover:text-slate-300 border border-slate-800'
            }`}
          >
            All ({alerts.length})
          </button>
          <button
            onClick={() => setSeverityFilter('High Danger')}
            className={`px-2.5 py-1 rounded-md text-[11px] font-bold shrink-0 transition-all flex items-center gap-1 ${
              severityFilter === 'High Danger'
                ? 'bg-red-500/30 text-red-200 border border-red-500/60 shadow-sm shadow-red-500/20'
                : 'bg-slate-900/60 text-red-400/80 hover:text-red-300 border border-slate-800'
            }`}
          >
            <span className="w-2 h-2 rounded-full bg-red-500"></span>
            High
          </button>
          <button
            onClick={() => setSeverityFilter('Moderate Warning')}
            className={`px-2.5 py-1 rounded-md text-[11px] font-bold shrink-0 transition-all flex items-center gap-1 ${
              severityFilter === 'Moderate Warning'
                ? 'bg-amber-500/30 text-amber-200 border border-amber-500/60 shadow-sm shadow-amber-500/20'
                : 'bg-slate-900/60 text-amber-400/80 hover:text-amber-300 border border-slate-800'
            }`}
          >
            <span className="w-2 h-2 rounded-full bg-amber-500"></span>
            Warning
          </button>
          <button
            onClick={() => setSeverityFilter('Safe')}
            className={`px-2.5 py-1 rounded-md text-[11px] font-bold shrink-0 transition-all flex items-center gap-1 ${
              severityFilter === 'Safe'
                ? 'bg-emerald-500/30 text-emerald-200 border border-emerald-500/60'
                : 'bg-slate-900/60 text-emerald-400/80 hover:text-emerald-300 border border-slate-800'
            }`}
          >
            <span className="w-2 h-2 rounded-full bg-emerald-500"></span>
            Safe
          </button>
        </div>
      </div>

      {/* Scrollable Event Feed */}
      <div className="flex-1 overflow-y-auto p-3 space-y-2.5">
        {filteredAlerts.length === 0 ? (
          <div className="h-48 flex flex-col items-center justify-center text-slate-500 text-xs">
            <ShieldCheck className="w-8 h-8 mb-2 text-slate-600" />
            <p>No active alerts match this filter</p>
          </div>
        ) : (
          filteredAlerts.map((alert) => {
            const isSelected = selectedAlert?.id === alert.id;

            return (
              <div
                key={alert.id}
                onClick={() => onSelectAlert(alert)}
                className={`p-3 rounded-xl border transition-all duration-200 cursor-pointer relative group ${
                  isSelected
                    ? 'bg-slate-800/90 border-cyan-500/60 shadow-lg shadow-cyan-950/40 ring-1 ring-cyan-500/40'
                    : 'bg-slate-900/60 hover:bg-slate-800/60 border-slate-800/80 hover:border-slate-700'
                }`}
              >
                {/* Active Selection Glow Bar */}
                {isSelected && (
                  <div className="absolute left-0 top-3 bottom-3 w-1 bg-cyan-400 rounded-r"></div>
                )}

                {/* Top Row: Category + Severity Pill + Time */}
                <div className="flex items-center justify-between gap-2 mb-1.5">
                  <div className="flex items-center gap-1.5">
                    {getCategoryIcon(alert.category)}
                    <span className="text-[11px] font-bold text-slate-300 tracking-wide uppercase">
                      {alert.category}
                    </span>
                  </div>

                  <div className="flex items-center gap-2">
                    <span
                      className={`px-2 py-0.5 rounded text-[10px] font-black uppercase tracking-wider ${
                        alert.severity === 'High Danger'
                          ? 'bg-red-500/20 text-red-400 border border-red-500/40'
                          : alert.severity === 'Moderate Warning'
                          ? 'bg-amber-500/20 text-amber-400 border border-amber-500/40'
                          : 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/40'
                      }`}
                    >
                      {alert.severity === 'High Danger' ? 'HIGH DANGER' : alert.severity === 'Moderate Warning' ? 'WARNING' : 'SAFE'}
                    </span>
                  </div>
                </div>

                {/* Event Title */}
                <h3 className="text-xs font-bold text-slate-100 group-hover:text-cyan-300 transition-colors leading-snug line-clamp-2">
                  {alert.title}
                </h3>

                {/* Location & Time */}
                <div className="flex items-center justify-between text-[11px] text-slate-400 mt-1.5">
                  <div className="flex items-center gap-1 text-slate-300 truncate max-w-[180px]">
                    <MapPin className="w-3 h-3 text-cyan-400 shrink-0" />
                    <span className="truncate">{alert.location}</span>
                  </div>
                  <div className="flex items-center gap-1 text-slate-400 shrink-0 font-mono text-[10px]">
                    <Clock className="w-2.5 h-2.5 text-slate-400" />
                    <span>{alert.timeAgo}</span>
                  </div>
                </div>

                {/* Risk Score Indicator Bar */}
                <div className="mt-2.5 pt-2 border-t border-slate-800/60">
                  <div className="flex items-center justify-between text-[10px] text-slate-400 mb-1">
                    <span>Severity Threat Index</span>
                    <span className="font-mono font-bold text-slate-200">{alert.riskScore}%</span>
                  </div>
                  <div className="w-full h-1.5 bg-slate-800 rounded-full overflow-hidden">
                    <div
                      className={`h-full rounded-full transition-all duration-500 ${
                        alert.riskScore > 75
                          ? 'bg-gradient-to-r from-orange-500 to-red-500'
                          : alert.riskScore > 45
                          ? 'bg-gradient-to-r from-yellow-500 to-amber-500'
                          : 'bg-gradient-to-r from-teal-500 to-emerald-500'
                      }`}
                      style={{ width: `${alert.riskScore}%` }}
                    ></div>
                  </div>
                </div>

                {/* Card Action Buttons */}
                <div className="flex items-center justify-between mt-2.5 pt-2 border-t border-slate-800/40">
                  <div className="flex items-center gap-1 text-[10px] text-emerald-400 font-medium truncate max-w-[170px]">
                    <Users className="w-3 h-3 shrink-0" />
                    <span className="truncate">{alert.ndrfDeployed}</span>
                  </div>

                  <div className="flex items-center gap-1">
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        onSelectAlert(alert);
                      }}
                      title="Focus on Map"
                      className="p-1 rounded bg-slate-800 hover:bg-cyan-900/50 hover:text-cyan-300 text-slate-400 text-[10px] transition-all flex items-center gap-1 px-1.5"
                    >
                      <Crosshair className="w-3 h-3 text-cyan-400" />
                      <span>Focus</span>
                    </button>
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        onOpenDetails(alert);
                      }}
                      title="View SitRep"
                      className="p-1 rounded bg-slate-800 hover:bg-slate-700 text-slate-300 text-[10px] transition-all flex items-center gap-1 px-1.5"
                    >
                      <FileText className="w-3 h-3 text-amber-400" />
                      <span>SitRep</span>
                    </button>
                  </div>
                </div>
              </div>
            );
          })
        )}
      </div>
    </aside>
  );
};
