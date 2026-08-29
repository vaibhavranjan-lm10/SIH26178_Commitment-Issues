import React from 'react';
import { 
  X, 
  ShieldAlert, 
  MapPin, 
  Clock, 
  AlertTriangle, 
  Users, 
  Waves, 
  Flame, 
  Activity, 
  Wind, 
  Mountain, 
  Tent, 
  Send, 
  Download, 
  FileText, 
  ExternalLink,
  CheckCircle2,
  PhoneCall
} from 'lucide-react';
import { DisasterAlert } from '../types/disaster';

interface IncidentDetailModalProps {
  alert: DisasterAlert | null;
  onClose: () => void;
  onOpenBroadcast: () => void;
}

export const IncidentDetailModal: React.FC<IncidentDetailModalProps> = ({
  alert,
  onClose,
  onOpenBroadcast,
}) => {
  if (!alert) return null;

  const getCategoryIcon = (category: string) => {
    switch (category) {
      case 'Floods': return <Waves className="w-5 h-5 text-blue-400" />;
      case 'Forest Fires': return <Flame className="w-5 h-5 text-orange-400" />;
      case 'Earthquakes': return <Activity className="w-5 h-5 text-yellow-400" />;
      case 'Cyclones': return <Wind className="w-5 h-5 text-cyan-400" />;
      case 'Landslides': return <Mountain className="w-5 h-5 text-amber-400" />;
      default: return <AlertTriangle className="w-5 h-5 text-red-400" />;
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-md animate-in fade-in duration-200">
      <div 
        className="bg-slate-900 border border-slate-700/80 rounded-2xl w-full max-w-2xl max-h-[90vh] flex flex-col shadow-2xl shadow-red-950/40 overflow-hidden"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Modal Header */}
        <div className="p-4 border-b border-slate-800 flex items-start justify-between gap-3 bg-slate-950/60">
          <div className="flex items-center gap-3">
            <div className="p-2.5 rounded-xl bg-slate-800 border border-slate-700">
              {getCategoryIcon(alert.category)}
            </div>
            <div>
              <div className="flex items-center gap-2">
                <span className="text-xs font-black uppercase tracking-wider text-cyan-400 font-mono">
                  {alert.id} &bull; {alert.category}
                </span>
                <span
                  className={`px-2 py-0.5 rounded text-[10px] font-black uppercase ${
                    alert.severity === 'High Danger'
                      ? 'bg-red-500/20 text-red-400 border border-red-500/40'
                      : alert.severity === 'Moderate Warning'
                      ? 'bg-amber-500/20 text-amber-400 border border-amber-500/40'
                      : 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/40'
                  }`}
                >
                  {alert.severity}
                </span>
              </div>
              <h2 className="text-base sm:text-lg font-bold text-white leading-snug mt-0.5">
                {alert.title}
              </h2>
            </div>
          </div>

          <button
            onClick={onClose}
            className="p-1.5 rounded-lg bg-slate-800/80 hover:bg-slate-700 text-slate-400 hover:text-white transition-all"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Modal Body */}
        <div className="p-4 sm:p-6 overflow-y-auto space-y-5 text-slate-200">
          {/* Location & Coordinates */}
          <div className="flex flex-wrap items-center justify-between gap-3 p-3 bg-slate-800/50 rounded-xl border border-slate-700/60 text-xs">
            <div className="flex items-center gap-2">
              <MapPin className="w-4 h-4 text-cyan-400" />
              <span>Location: <strong className="text-white">{alert.location}</strong></span>
            </div>
            <div className="flex items-center gap-2 text-slate-400 font-mono text-[11px]">
              <Clock className="w-3.5 h-3.5 text-slate-400" />
              <span>Timestamp: {alert.timestamp} ({alert.timeAgo})</span>
            </div>
          </div>

          {/* Description */}
          <div>
            <h4 className="text-xs font-black uppercase tracking-wider text-slate-400 mb-1.5">
              Incident Situation Summary
            </h4>
            <p className="text-sm text-slate-300 leading-relaxed bg-slate-950/40 p-3.5 rounded-xl border border-slate-800">
              {alert.description}
            </p>
          </div>

          {/* Telemetry Metrics Grid */}
          <div>
            <h4 className="text-xs font-black uppercase tracking-wider text-slate-400 mb-2">
              Sensor Telemetry & Field Metrics
            </h4>
            <div className="grid grid-cols-2 sm:grid-cols-3 gap-2.5 text-xs">
              <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                <span className="text-slate-400 block text-[11px]">Threat Score</span>
                <span className="text-lg font-black text-red-400 font-mono">{alert.riskScore} / 100</span>
              </div>
              <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                <span className="text-slate-400 block text-[11px]">Affected Pop.</span>
                <span className="text-lg font-black text-white font-mono">{alert.affectedPopulation}</span>
              </div>
              {alert.details.riverLevel && (
                <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                  <span className="text-slate-400 block text-[11px]">River Gauge Level</span>
                  <span className="text-lg font-black text-blue-300 font-mono">{alert.details.riverLevel}</span>
                </div>
              )}
              {alert.details.windSpeed && (
                <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                  <span className="text-slate-400 block text-[11px]">Sustained Wind</span>
                  <span className="text-lg font-black text-cyan-300 font-mono">{alert.details.windSpeed}</span>
                </div>
              )}
              {alert.details.magnitude && (
                <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                  <span className="text-slate-400 block text-[11px]">Seismic Magnitude</span>
                  <span className="text-lg font-black text-amber-300 font-mono">{alert.details.magnitude}</span>
                </div>
              )}
              {alert.details.rainfall && (
                <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                  <span className="text-slate-400 block text-[11px]">Precipitation</span>
                  <span className="text-lg font-black text-emerald-300 font-mono">{alert.details.rainfall}</span>
                </div>
              )}
              {alert.details.evacuationCount && (
                <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700">
                  <span className="text-slate-400 block text-[11px]">Evacuated Citizens</span>
                  <span className="text-lg font-black text-teal-300 font-mono">{alert.details.evacuationCount}</span>
                </div>
              )}
              <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700 col-span-2 sm:col-span-1">
                <span className="text-slate-400 block text-[11px]">NDRF Task Force</span>
                <span className="text-sm font-black text-emerald-400 font-mono">{alert.ndrfDeployed}</span>
              </div>
            </div>
          </div>

          {/* Action Recommendations */}
          <div>
            <h4 className="text-xs font-black uppercase tracking-wider text-slate-400 mb-2">
              Command Center Standard Operating Directives
            </h4>
            <div className="space-y-2">
              {alert.recommendations.map((rec, idx) => (
                <div key={idx} className="flex items-start gap-2.5 p-2.5 rounded-lg bg-slate-800/40 border border-slate-700/50 text-xs">
                  <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0 mt-0.5" />
                  <span>{rec}</span>
                </div>
              ))}
            </div>
          </div>
        </div>

        {/* Modal Footer Actions */}
        <div className="p-4 border-t border-slate-800 bg-slate-950/80 flex flex-wrap items-center justify-between gap-3">
          <div className="flex items-center gap-2 text-xs text-slate-400">
            <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse"></span>
            NDMA Response Protocols Engaged
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={() => {
                onClose();
                onOpenBroadcast();
              }}
              className="px-3.5 py-2 rounded-xl bg-gradient-to-r from-cyan-600 to-blue-600 hover:from-cyan-500 hover:to-blue-500 text-white text-xs font-bold shadow-lg shadow-cyan-950/50 flex items-center gap-2 transition-all"
            >
              <Send className="w-3.5 h-3.5" />
              <span>Broadcast Citizen Alert (CAP)</span>
            </button>
            <button
              onClick={onClose}
              className="px-3.5 py-2 rounded-xl bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-bold border border-slate-700 transition-all"
            >
              Close
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
