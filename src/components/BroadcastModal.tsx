import React, { useState } from 'react';
import { 
  X, 
  Send, 
  Radio, 
  CheckCircle2, 
  AlertTriangle, 
  ShieldAlert, 
  MessageSquare, 
  Users, 
  Sparkles,
  Smartphone
} from 'lucide-react';
import { DisasterAlert } from '../types/disaster';

interface BroadcastModalProps {
  isOpen: boolean;
  onClose: () => void;
  selectedAlert: DisasterAlert | null;
}

export const BroadcastModal: React.FC<BroadcastModalProps> = ({
  isOpen,
  onClose,
  selectedAlert,
}) => {
  const [broadcastTarget, setBroadcastTarget] = useState<'All Zones' | 'High Danger Zones' | 'Current Incident'>('Current Incident');
  const [broadcastChannel, setBroadcastChannel] = useState<'SMS & Cell Broadcast' | 'Common Alerting Protocol (CAP)' | 'Radio / Siren Grid'>('SMS & Cell Broadcast');
  const [customMessage, setCustomMessage] = useState<string>(
    selectedAlert 
      ? `EMERGENCY ALERT: ${selectedAlert.title} in ${selectedAlert.location}. Citizens are advised to seek designated shelter immediately. For assistance call NDRF 1078.`
      : 'NATIONAL DISASTER ALERT: High risk conditions detected in your sector. Stay indoors and monitor official NDMA updates on radio/TV. Emergency Helpline: 1078.'
  );
  const [isSent, setIsSent] = useState<boolean>(false);
  const [isSending, setIsSending] = useState<boolean>(false);

  if (!isOpen) return null;

  const handleSend = () => {
    setIsSending(true);
    setTimeout(() => {
      setIsSending(false);
      setIsSent(true);
      setTimeout(() => {
        setIsSent(false);
        onClose();
      }, 2500);
    }, 1200);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-md animate-in fade-in duration-200">
      <div 
        className="bg-slate-900 border border-slate-700/80 rounded-2xl w-full max-w-lg shadow-2xl overflow-hidden"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-4 border-b border-slate-800 flex items-center justify-between bg-slate-950/60">
          <div className="flex items-center gap-2.5">
            <div className="p-2 rounded-xl bg-cyan-500/20 text-cyan-400 border border-cyan-500/30">
              <Radio className="w-5 h-5 animate-pulse" />
            </div>
            <div>
              <h3 className="text-sm font-black text-white uppercase tracking-wider">
                Emergency Alert Broadcast (CAP)
              </h3>
              <p className="text-[11px] text-slate-400">
                National Multi-Hazard Early Warning Network
              </p>
            </div>
          </div>

          <button
            onClick={onClose}
            className="p-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-400 hover:text-white"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Content */}
        <div className="p-5 space-y-4 text-xs text-slate-300">
          {/* Target Zone */}
          <div>
            <label className="text-[11px] font-bold text-slate-300 block mb-1.5 uppercase tracking-wide">
              Target Audience & Geo-Fence:
            </label>
            <div className="grid grid-cols-3 gap-2">
              {(['Current Incident', 'High Danger Zones', 'All Zones'] as const).map((t) => (
                <button
                  key={t}
                  onClick={() => setBroadcastTarget(t)}
                  className={`py-2 px-2.5 rounded-lg border text-center font-bold text-[11px] transition-all ${
                    broadcastTarget === t
                      ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300'
                      : 'bg-slate-800/60 border-slate-700 text-slate-400 hover:text-slate-200'
                  }`}
                >
                  {t}
                </button>
              ))}
            </div>
          </div>

          {/* Delivery Channel */}
          <div>
            <label className="text-[11px] font-bold text-slate-300 block mb-1.5 uppercase tracking-wide">
              Transmission Protocol:
            </label>
            <select
              value={broadcastChannel}
              onChange={(e) => setBroadcastChannel(e.target.value as any)}
              className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-xs text-slate-200 focus:outline-none focus:border-cyan-500/50"
            >
              <option value="SMS & Cell Broadcast">Cell Broadcast Service (CBS) & Multi-carrier SMS</option>
              <option value="Common Alerting Protocol (CAP)">Common Alerting Protocol (CAP) Web Feeds</option>
              <option value="Radio / Siren Grid">All India Radio & Local Siren Array</option>
            </select>
          </div>

          {/* Message Textarea */}
          <div>
            <label className="text-[11px] font-bold text-slate-300 block mb-1.5 uppercase tracking-wide">
              Alert Message Dispatch:
            </label>
            <textarea
              rows={4}
              value={customMessage}
              onChange={(e) => setCustomMessage(e.target.value)}
              className="w-full bg-slate-950/70 border border-slate-700 rounded-xl p-3 text-xs text-slate-100 font-sans focus:outline-none focus:border-cyan-500/60 transition-all resize-none"
            />
            <span className="text-[10px] text-slate-500 block mt-1">
              Characters: {customMessage.length} / 160 GSM standard
            </span>
          </div>

          {/* Estimated Reach Card */}
          <div className="p-3 bg-slate-800/60 rounded-xl border border-slate-700/60 flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Smartphone className="w-4 h-4 text-emerald-400" />
              <span className="text-slate-300">Estimated Instant Reach:</span>
            </div>
            <span className="font-mono font-black text-emerald-400 text-sm">
              ~680,000+ Mobile Devices
            </span>
          </div>
        </div>

        {/* Footer */}
        <div className="p-4 border-t border-slate-800 bg-slate-950/80 flex items-center justify-end gap-2.5">
          <button
            onClick={onClose}
            className="px-3.5 py-2 rounded-xl bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-bold transition-all"
          >
            Cancel
          </button>
          <button
            onClick={handleSend}
            disabled={isSending || isSent}
            className="px-4 py-2 rounded-xl bg-gradient-to-r from-red-600 via-orange-600 to-amber-600 hover:from-red-500 hover:to-orange-500 text-white text-xs font-black shadow-lg shadow-red-950/50 flex items-center gap-2 transition-all disabled:opacity-50"
          >
            {isSending ? (
              <>
                <Radio className="w-4 h-4 animate-spin" />
                <span>Broadcasting to Telco Towers...</span>
              </>
            ) : isSent ? (
              <>
                <CheckCircle2 className="w-4 h-4 text-emerald-300" />
                <span>Alert Dispatched Successfully!</span>
              </>
            ) : (
              <>
                <Send className="w-4 h-4" />
                <span>Authorize & Dispatch Alert</span>
              </>
            )}
          </button>
        </div>
      </div>
    </div>
  );
};
