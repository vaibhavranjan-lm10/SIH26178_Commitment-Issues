import React from 'react';
import { 
  X, 
  ShieldAlert, 
  Radio, 
  MapPin, 
  Clock, 
  ChevronRight, 
  Crosshair, 
  Flame, 
  Waves, 
  Activity, 
  Wind, 
  Mountain,
  AlertTriangle
} from 'lucide-react';
import { DisasterAlert } from '../types/disaster';

interface LiveAlertsModalProps {
  isOpen: boolean;
  onClose: () => void;
  alerts: DisasterAlert[];
  onSelectAlert: (alert: DisasterAlert) => void;
  onOpenDetails: (alert: DisasterAlert) => void;
}

export const LiveAlertsModal: React.FC<LiveAlertsModalProps> = ({
  isOpen,
  onClose,
  alerts,
  onSelectAlert,
  onOpenDetails,
}) => {
  if (!isOpen) return null;

  const criticalAlerts = alerts.filter(a => a.severity === 'High Danger');
  const warningAlerts = alerts.filter(a => a.severity === 'Moderate Warning');
  const safeAlerts = alerts.filter(a => a.severity === 'Safe');

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
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-md animate-in fade-in duration-200">
      <div 
        className="bg-slate-900 border border-slate-700/80 rounded-2xl w-full max-w-3xl max-h-[85vh] flex flex-col shadow-2xl overflow-hidden"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-4 border-b border-slate-800 flex items-center justify-between bg-slate-950/60">
          <div className="flex items-center gap-3">
            <div className="p-2.5 rounded-xl bg-red-500/20 text-red-400 border border-red-500/40">
              <ShieldAlert className="w-5 h-5 animate-pulse" />
            </div>
            <div>
              <h2 className="text-base font-black text-white uppercase tracking-wider flex items-center gap-2">
                ACTIVE NATIONAL ALERTS DISPATCH
                <span className="px-2 py-0.5 rounded-full text-xs font-black bg-red-500 text-white font-mono">
                  {alerts.length}
                </span>
              </h2>
              <p className="text-[11px] text-slate-400">
                Multi-agency aggregated incidents &bull; IMD, CWC, ISRO, NDMA
              </p>
            </div>
          </div>

          <button
            onClick={onClose}
            className="p-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-400 hover:text-white"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Content list */}
        <div className="p-4 overflow-y-auto space-y-4">
          {/* High Danger Section */}
          {criticalAlerts.length > 0 && (
            <div>
              <div className="flex items-center gap-2 mb-2.5">
                <span className="w-2.5 h-2.5 rounded-full bg-red-500 animate-ping"></span>
                <h3 className="text-xs font-black uppercase tracking-wider text-red-400">
                  Critical High Danger Zones ({criticalAlerts.length})
                </h3>
              </div>

              <div className="space-y-2">
                {criticalAlerts.map((alert) => (
                  <div
                    key={alert.id}
                    className="p-3 rounded-xl bg-red-950/20 border border-red-500/30 hover:border-red-500/60 transition-all flex flex-col sm:flex-row sm:items-center justify-between gap-3"
                  >
                    <div className="flex items-start gap-3">
                      <div className="p-2 rounded-lg bg-slate-900 border border-slate-800 shrink-0 mt-0.5">
                        {getCategoryIcon(alert.category)}
                      </div>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-xs font-bold text-white">{alert.title}</span>
                          <span className="text-[10px] font-black px-1.5 py-0.5 rounded bg-red-500/30 text-red-300 border border-red-500/50">
                            SCORE {alert.riskScore}
                          </span>
                        </div>
                        <p className="text-[11px] text-cyan-300 mt-0.5 flex items-center gap-1">
                          <MapPin className="w-3 h-3 text-cyan-400" />
                          {alert.location} &bull; {alert.affectedPopulation} affected
                        </p>
                      </div>
                    </div>

                    <div className="flex items-center gap-2 shrink-0 self-end sm:self-center">
                      <button
                        onClick={() => {
                          onSelectAlert(alert);
                          onClose();
                        }}
                        className="px-2.5 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-semibold flex items-center gap-1"
                      >
                        <Crosshair className="w-3.5 h-3.5 text-cyan-400" />
                        <span>Map Focus</span>
                      </button>
                      <button
                        onClick={() => {
                          onClose();
                          onOpenDetails(alert);
                        }}
                        className="px-2.5 py-1.5 rounded-lg bg-red-600 hover:bg-red-500 text-white text-xs font-semibold flex items-center gap-1 shadow-sm"
                      >
                        <span>SitRep</span>
                        <ChevronRight className="w-3.5 h-3.5" />
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Moderate Warning Section */}
          {warningAlerts.length > 0 && (
            <div>
              <div className="flex items-center gap-2 mb-2.5">
                <span className="w-2.5 h-2.5 rounded-full bg-amber-500"></span>
                <h3 className="text-xs font-black uppercase tracking-wider text-amber-400">
                  Moderate Warning Zones ({warningAlerts.length})
                </h3>
              </div>

              <div className="space-y-2">
                {warningAlerts.map((alert) => (
                  <div
                    key={alert.id}
                    className="p-3 rounded-xl bg-amber-950/20 border border-amber-500/30 hover:border-amber-500/60 transition-all flex flex-col sm:flex-row sm:items-center justify-between gap-3"
                  >
                    <div className="flex items-start gap-3">
                      <div className="p-2 rounded-lg bg-slate-900 border border-slate-800 shrink-0 mt-0.5">
                        {getCategoryIcon(alert.category)}
                      </div>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-xs font-bold text-white">{alert.title}</span>
                          <span className="text-[10px] font-black px-1.5 py-0.5 rounded bg-amber-500/30 text-amber-300 border border-amber-500/50">
                            SCORE {alert.riskScore}
                          </span>
                        </div>
                        <p className="text-[11px] text-cyan-300 mt-0.5 flex items-center gap-1">
                          <MapPin className="w-3 h-3 text-cyan-400" />
                          {alert.location}
                        </p>
                      </div>
                    </div>

                    <div className="flex items-center gap-2 shrink-0 self-end sm:self-center">
                      <button
                        onClick={() => {
                          onSelectAlert(alert);
                          onClose();
                        }}
                        className="px-2.5 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-semibold flex items-center gap-1"
                      >
                        <Crosshair className="w-3.5 h-3.5 text-cyan-400" />
                        <span>Map Focus</span>
                      </button>
                      <button
                        onClick={() => {
                          onClose();
                          onOpenDetails(alert);
                        }}
                        className="px-2.5 py-1.5 rounded-lg bg-amber-600 hover:bg-amber-500 text-white text-xs font-semibold flex items-center gap-1"
                      >
                        <span>SitRep</span>
                        <ChevronRight className="w-3.5 h-3.5" />
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Safe Monitored Section */}
          {safeAlerts.length > 0 && (
            <div>
              <div className="flex items-center gap-2 mb-2.5">
                <span className="w-2.5 h-2.5 rounded-full bg-emerald-500"></span>
                <h3 className="text-xs font-black uppercase tracking-wider text-emerald-400">
                  Monitored / Safe Zones ({safeAlerts.length})
                </h3>
              </div>

              <div className="space-y-2">
                {safeAlerts.map((alert) => (
                  <div
                    key={alert.id}
                    className="p-3 rounded-xl bg-emerald-950/20 border border-emerald-500/30 hover:border-emerald-500/60 transition-all flex flex-col sm:flex-row sm:items-center justify-between gap-3"
                  >
                    <div className="flex items-start gap-3">
                      <div className="p-2 rounded-lg bg-slate-900 border border-slate-800 shrink-0 mt-0.5">
                        {getCategoryIcon(alert.category)}
                      </div>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-xs font-bold text-white">{alert.title}</span>
                          <span className="text-[10px] font-black px-1.5 py-0.5 rounded bg-emerald-500/30 text-emerald-300 border border-emerald-500/50">
                            STABLE
                          </span>
                        </div>
                        <p className="text-[11px] text-slate-400 mt-0.5 flex items-center gap-1">
                          <MapPin className="w-3 h-3 text-emerald-400" />
                          {alert.location}
                        </p>
                      </div>
                    </div>

                    <div className="flex items-center gap-2 shrink-0 self-end sm:self-center">
                      <button
                        onClick={() => {
                          onSelectAlert(alert);
                          onClose();
                        }}
                        className="px-2.5 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-semibold flex items-center gap-1"
                      >
                        <Crosshair className="w-3.5 h-3.5 text-cyan-400" />
                        <span>Map Focus</span>
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
