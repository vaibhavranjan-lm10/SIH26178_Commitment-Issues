import React, { useState } from 'react';
import { 
  ResponsiveContainer, 
  AreaChart, 
  Area, 
  XAxis, 
  YAxis, 
  Tooltip as RechartsTooltip, 
  CartesianGrid,
  LineChart,
  Line
} from 'recharts';
import { 
  Activity, 
  ShieldAlert, 
  Users, 
  Tent, 
  PhoneCall, 
  TrendingUp, 
  Filter, 
  ExternalLink, 
  Copy, 
  Check, 
  ChevronRight, 
  AlertOctagon,
  Building2,
  LifeBuoy,
  Compass,
  Radio,
  Sliders
} from 'lucide-react';
import { DisasterCategory, EmergencyContact, KeyMetrics, SeverityLevel, TrendDataPoint } from '../types/disaster';

interface RightMetricsPanelProps {
  metrics: KeyMetrics;
  trendData: TrendDataPoint[];
  contacts: EmergencyContact[];
  selectedCategory: DisasterCategory;
  onSelectCategory: (category: DisasterCategory) => void;
  selectedState: string;
  onSelectState: (state: string) => void;
  statesList: string[];
  onOpenReportModal: () => void;
}

export const RightMetricsPanel: React.FC<RightMetricsPanelProps> = ({
  metrics,
  trendData,
  contacts,
  selectedCategory,
  onSelectCategory,
  selectedState,
  onSelectState,
  statesList,
  onOpenReportModal,
}) => {
  const [copiedId, setCopiedId] = useState<string | null>(null);
  const [chartMetric, setChartMetric] = useState<'floods' | 'severityIndex' | 'cyclones'>('severityIndex');

  const handleCopyPhone = (id: string, phone: string) => {
    navigator.clipboard.writeText(phone);
    setCopiedId(id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  const getContactIcon = (iconName: string) => {
    switch (iconName) {
      case 'ShieldAlert': return <ShieldAlert className="w-4 h-4 text-red-400" />;
      case 'Building2': return <Building2 className="w-4 h-4 text-blue-400" />;
      case 'LifeBuoy': return <LifeBuoy className="w-4 h-4 text-amber-400" />;
      case 'Compass': return <Compass className="w-4 h-4 text-emerald-400" />;
      default: return <PhoneCall className="w-4 h-4 text-cyan-400" />;
    }
  };

  return (
    <aside className="w-full md:w-80 lg:w-[360px] xl:w-[380px] h-full flex flex-col bg-[#0b0f19]/90 border-l border-slate-800/80 backdrop-blur-xl shrink-0 z-20 overflow-y-auto select-none p-3.5 space-y-4">
      {/* 1. KEY METRICS CARD */}
      <div className="bg-slate-900/80 border border-slate-800/90 rounded-2xl p-4 shadow-lg relative overflow-hidden">
        <div className="absolute top-0 right-0 w-32 h-32 bg-red-500/5 rounded-full blur-2xl pointer-events-none"></div>

        {/* Header */}
        <div className="flex items-center justify-between mb-3.5 pb-2 border-b border-slate-800">
          <div className="flex items-center gap-2">
            <Activity className="w-4 h-4 text-red-400 animate-pulse" />
            <h3 className="text-xs font-black uppercase tracking-wider text-slate-100 flex items-center gap-1">
              Key Metrics <ChevronRight className="w-3.5 h-3.5 text-slate-500" />
            </h3>
          </div>
          <span className="text-[10px] text-emerald-400 font-mono flex items-center gap-1">
            <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-ping"></span>
            LIVE TELEMETRY
          </span>
        </div>

        {/* 4-Stat Grid */}
        <div className="grid grid-cols-2 gap-2.5">
          {/* Total Alerts */}
          <div className="p-2.5 rounded-xl bg-slate-800/50 border border-slate-700/50">
            <span className="text-[10px] uppercase font-bold text-slate-400 block">Total Alerts</span>
            <div className="flex items-baseline gap-2 mt-1">
              <span className="text-xl font-black text-white font-mono">{metrics.totalActiveAlerts}</span>
              <span className="text-[10px] text-red-400 font-bold">+{metrics.highDangerCount} Critical</span>
            </div>
          </div>

          {/* High Danger Zones */}
          <div className="p-2.5 rounded-xl bg-red-950/20 border border-red-500/30">
            <span className="text-[10px] uppercase font-bold text-red-300 block">High Danger</span>
            <div className="flex items-baseline gap-2 mt-1">
              <span className="text-xl font-black text-red-400 font-mono">{metrics.highDangerCount}</span>
              <span className="text-[10px] text-red-300/80">Zones</span>
            </div>
          </div>

          {/* Relief Camps */}
          <div className="p-2.5 rounded-xl bg-slate-800/50 border border-slate-700/50">
            <span className="text-[10px] uppercase font-bold text-slate-400 block">Relief Camps</span>
            <div className="flex items-baseline gap-1.5 mt-1">
              <Tent className="w-3.5 h-3.5 text-cyan-400" />
              <span className="text-xl font-black text-cyan-300 font-mono">{metrics.reliefCampsActive}</span>
              <span className="text-[10px] text-slate-400">Active</span>
            </div>
          </div>

          {/* NDRF Personnel */}
          <div className="p-2.5 rounded-xl bg-slate-800/50 border border-slate-700/50">
            <span className="text-[10px] uppercase font-bold text-slate-400 block">NDRF Deployed</span>
            <div className="flex items-baseline gap-1.5 mt-1">
              <Users className="w-3.5 h-3.5 text-emerald-400" />
              <span className="text-xl font-black text-emerald-300 font-mono">{metrics.ndrfPersonnelDeployed}</span>
            </div>
          </div>
        </div>

        {/* National Risk Index Gauge Bar */}
        <div className="mt-3 pt-3 border-t border-slate-800/60">
          <div className="flex items-center justify-between text-xs mb-1.5">
            <span className="text-slate-300 font-medium">National Risk Index</span>
            <span className="font-mono font-black text-red-400">{metrics.nationalRiskIndex} / 100</span>
          </div>
          <div className="w-full h-2 bg-slate-800 rounded-full overflow-hidden p-0.5 border border-slate-700/60">
            <div
              className="h-full rounded-full bg-gradient-to-r from-emerald-500 via-amber-500 to-red-500 transition-all duration-700"
              style={{ width: `${metrics.nationalRiskIndex}%` }}
            ></div>
          </div>
        </div>
      </div>

      {/* 2. DISASTER TRENDS MINI CHART (RECHARTS) */}
      <div className="bg-slate-900/80 border border-slate-800/90 rounded-2xl p-4 shadow-lg">
        <div className="flex items-center justify-between mb-3 pb-2 border-b border-slate-800">
          <div className="flex items-center gap-2">
            <TrendingUp className="w-4 h-4 text-cyan-400" />
            <h3 className="text-xs font-black uppercase tracking-wider text-slate-100">
              Disaster Trends (24h)
            </h3>
          </div>

          {/* Metric switch */}
          <div className="flex items-center gap-1 bg-slate-800/80 p-0.5 rounded-md text-[10px] font-bold">
            <button
              onClick={() => setChartMetric('severityIndex')}
              className={`px-1.5 py-0.5 rounded ${
                chartMetric === 'severityIndex' ? 'bg-cyan-500 text-slate-900 font-black' : 'text-slate-400'
              }`}
            >
              Risk
            </button>
            <button
              onClick={() => setChartMetric('floods')}
              className={`px-1.5 py-0.5 rounded ${
                chartMetric === 'floods' ? 'bg-cyan-500 text-slate-900 font-black' : 'text-slate-400'
              }`}
            >
              Floods
            </button>
            <button
              onClick={() => setChartMetric('cyclones')}
              className={`px-1.5 py-0.5 rounded ${
                chartMetric === 'cyclones' ? 'bg-cyan-500 text-slate-900 font-black' : 'text-slate-400'
              }`}
            >
              Cyclones
            </button>
          </div>
        </div>

        {/* Recharts Area Chart */}
        <div className="h-36 w-full">
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={trendData} margin={{ top: 5, right: 5, left: -25, bottom: 0 }}>
              <defs>
                <linearGradient id="colorRisk" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#ef4444" stopOpacity={0.4} />
                  <stop offset="95%" stopColor="#ef4444" stopOpacity={0.0} />
                </linearGradient>
                <linearGradient id="colorFloods" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#06b6d4" stopOpacity={0.4} />
                  <stop offset="95%" stopColor="#06b6d4" stopOpacity={0.0} />
                </linearGradient>
                <linearGradient id="colorCyclones" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#3b82f6" stopOpacity={0.4} />
                  <stop offset="95%" stopColor="#3b82f6" stopOpacity={0.0} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
              <XAxis dataKey="time" stroke="#64748b" tick={{ fontSize: 10 }} />
              <YAxis stroke="#64748b" tick={{ fontSize: 10 }} domain={[0, 'auto']} />
              <RechartsTooltip
                contentStyle={{
                  backgroundColor: '#0f172a',
                  borderColor: '#334155',
                  borderRadius: '8px',
                  fontSize: '11px',
                  boxShadow: '0 4px 12px rgba(0,0,0,0.5)',
                }}
                labelStyle={{ color: '#94a3b8', fontWeight: 'bold' }}
              />
              {chartMetric === 'severityIndex' && (
                <Area
                  type="monotone"
                  dataKey="severityIndex"
                  name="Severity Index"
                  stroke="#ef4444"
                  strokeWidth={2}
                  fillOpacity={1}
                  fill="url(#colorRisk)"
                />
              )}
              {chartMetric === 'floods' && (
                <Area
                  type="monotone"
                  dataKey="floods"
                  name="Active Floods"
                  stroke="#06b6d4"
                  strokeWidth={2}
                  fillOpacity={1}
                  fill="url(#colorFloods)"
                />
              )}
              {chartMetric === 'cyclones' && (
                <Area
                  type="monotone"
                  dataKey="cyclones"
                  name="Cyclone Alerts"
                  stroke="#3b82f6"
                  strokeWidth={2}
                  fillOpacity={1}
                  fill="url(#colorCyclones)"
                />
              )}
            </AreaChart>
          </ResponsiveContainer>
        </div>

        <div className="flex items-center justify-between text-[10px] text-slate-400 mt-2 font-mono">
          <span>SOURCE: IMD / CWC REAL-TIME</span>
          <span className="text-cyan-400">Peak Surge: +24%</span>
        </div>
      </div>

      {/* 3. FILTERS SECTION */}
      <div className="bg-slate-900/80 border border-slate-800/90 rounded-2xl p-4 shadow-lg space-y-3">
        <div className="flex items-center justify-between pb-2 border-b border-slate-800">
          <div className="flex items-center gap-2">
            <Filter className="w-4 h-4 text-amber-400" />
            <h3 className="text-xs font-black uppercase tracking-wider text-slate-100">
              Filters & Parameters
            </h3>
          </div>
          {(selectedCategory !== 'All' || selectedState !== 'All States') && (
            <button
              onClick={() => {
                onSelectCategory('All');
                onSelectState('All States');
              }}
              className="text-[10px] text-cyan-400 hover:text-cyan-300 font-semibold"
            >
              Reset Filters
            </button>
          )}
        </div>

        {/* State Filter Dropdown */}
        <div>
          <label className="text-[11px] font-bold text-slate-300 block mb-1.5">
            Geographic State / UT:
          </label>
          <select
            value={selectedState}
            onChange={(e) => onSelectState(e.target.value)}
            className="w-full bg-slate-800 border border-slate-700 rounded-lg px-2.5 py-1.5 text-xs text-slate-200 focus:outline-none focus:border-cyan-500/50"
          >
            <option value="All States">All States & Union Territories (India)</option>
            {statesList.map((st) => (
              <option key={st} value={st}>
                {st}
              </option>
            ))}
          </select>
        </div>

        {/* Quick Category Chips */}
        <div>
          <label className="text-[11px] font-bold text-slate-300 block mb-1.5">
            Disaster Category:
          </label>
          <div className="flex flex-wrap gap-1.5">
            {(['All', 'Floods', 'Forest Fires', 'Earthquakes', 'Cyclones', 'Landslides'] as DisasterCategory[]).map((cat) => (
              <button
                key={cat}
                onClick={() => onSelectCategory(cat)}
                className={`px-2 py-1 rounded-md text-[10px] font-bold transition-all ${
                  selectedCategory === cat
                    ? 'bg-cyan-500 text-slate-950 font-black shadow-sm'
                    : 'bg-slate-800 text-slate-300 hover:bg-slate-700 border border-slate-700/50'
                }`}
              >
                {cat}
              </button>
            ))}
          </div>
        </div>
      </div>

      {/* 4. EMERGENCY CONTACTS CARD */}
      <div className="bg-slate-900/80 border border-slate-800/90 rounded-2xl p-4 shadow-lg space-y-2.5">
        <div className="flex items-center justify-between pb-2 border-b border-slate-800">
          <div className="flex items-center gap-2">
            <PhoneCall className="w-4 h-4 text-emerald-400" />
            <h3 className="text-xs font-black uppercase tracking-wider text-slate-100 flex items-center gap-1">
              Emergency Contacts <ChevronRight className="w-3.5 h-3.5 text-slate-500" />
            </h3>
          </div>
          <span className="text-[10px] text-emerald-400 font-bold bg-emerald-500/10 px-1.5 py-0.5 rounded border border-emerald-500/20">
            24x7 HOTLINES
          </span>
        </div>

        {/* Contacts List */}
        <div className="space-y-2">
          {contacts.map((c) => (
            <div
              key={c.id}
              className="p-2.5 rounded-xl bg-slate-800/50 hover:bg-slate-800/80 border border-slate-700/50 transition-all flex items-center justify-between gap-2"
            >
              <div className="flex items-center gap-2.5 min-w-0">
                <div className="p-2 rounded-lg bg-slate-900 border border-slate-700 shrink-0">
                  {getContactIcon(c.iconName)}
                </div>
                <div className="min-w-0">
                  <div className="flex items-center gap-1.5">
                    <span className="text-xs font-bold text-slate-200 truncate">{c.name}</span>
                  </div>
                  <span className="text-[10px] text-slate-400 block truncate">{c.agency}</span>
                  <span className="text-xs font-mono font-black text-cyan-300 block mt-0.5">
                    {c.phone}
                  </span>
                </div>
              </div>

              {/* Copy / Call Action */}
              <button
                onClick={() => handleCopyPhone(c.id, c.phone)}
                title="Copy Number"
                className="p-2 rounded-lg bg-slate-900 hover:bg-slate-700 text-slate-300 hover:text-white border border-slate-700 shrink-0 transition-all"
              >
                {copiedId === c.id ? (
                  <Check className="w-3.5 h-3.5 text-emerald-400" />
                ) : (
                  <Copy className="w-3.5 h-3.5 text-slate-400" />
                )}
              </button>
            </div>
          ))}
        </div>
      </div>
    </aside>
  );
};
