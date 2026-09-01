import React, { useEffect, useMemo, useState } from 'react';
import { MapContainer, TileLayer, Marker, Popup, Tooltip, useMap, CircleMarker } from 'react-leaflet';
import L from 'leaflet';
import { 
  AlertTriangle, 
  ShieldAlert, 
  MapPin, 
  Users, 
  Crosshair, 
  Maximize2, 
  Layers, 
  Eye, 
  EyeOff, 
  RotateCcw, 
  CheckCircle2, 
  Flame, 
  Waves, 
  Activity, 
  Wind, 
  Mountain 
} from 'lucide-react';
import { DisasterAlert, SeverityLevel } from '../types/disaster';

interface DisasterMapProps {
  alerts: DisasterAlert[];
  selectedAlert: DisasterAlert | null;
  onSelectAlert: (alert: DisasterAlert) => void;
  onOpenDetails: (alert: DisasterAlert) => void;
}

// Controller component to handle smooth animated pan/zoom
const MapController: React.FC<{
  center: [number, number];
  zoom: number;
  selectedCoordinates?: [number, number] | null;
}> = ({ center, zoom, selectedCoordinates }) => {
  const map = useMap();

  useEffect(() => {
    if (selectedCoordinates) {
      map.flyTo(selectedCoordinates, 8, {
        duration: 1.5,
        easeLinearity: 0.25,
      });
    }
  }, [selectedCoordinates, map]);

  return null;
};

// Mouse coordinate tracker for Mission Control HUD
const MousePositionHUD: React.FC = () => {
  const [coords, setCoords] = useState<{ lat: number; lng: number }>({ lat: 20.5937, lng: 78.9629 });
  const map = useMap();

  useEffect(() => {
    const handleMouseMove = (e: L.LeafletMouseEvent) => {
      setCoords({
        lat: parseFloat(e.latlng.lat.toFixed(4)),
        lng: parseFloat(e.latlng.lng.toFixed(4)),
      });
    };
    map.on('mousemove', handleMouseMove);
    return () => {
      map.off('mousemove', handleMouseMove);
    };
  }, [map]);

  return (
    <div className="absolute bottom-4 right-4 z-[400] bg-slate-900/90 border border-slate-800/90 backdrop-blur-md px-3 py-1.5 rounded-lg text-[11px] font-mono text-cyan-300 shadow-xl flex items-center gap-3 select-none">
      <div className="flex items-center gap-1.5">
        <Crosshair className="w-3.5 h-3.5 text-cyan-400 animate-spin" style={{ animationDuration: '8s' }} />
        <span>RADAR HUD:</span>
      </div>
      <span className="text-slate-400">LAT: <strong className="text-white">{coords.lat}° N</strong></span>
      <span className="text-slate-400">LNG: <strong className="text-white">{coords.lng}° E</strong></span>
      <span className="inline-block w-1.5 h-1.5 rounded-full bg-emerald-400 animate-ping"></span>
    </div>
  );
};

export const DisasterMap: React.FC<DisasterMapProps> = ({
  alerts,
  selectedAlert,
  onSelectAlert,
  onOpenDetails,
}) => {
  const defaultCenter: [number, number] = [20.5937, 78.9629];
  const defaultZoom = 5;

  const [mapLayer, setMapLayer] = useState<'dark' | 'satellite' | 'street'>('dark');
  const [showHeatCircles, setShowHeatCircles] = useState<boolean>(true);
  const [resetCounter, setResetCounter] = useState<number>(0);

  // Map Tile URLs
  const tileLayers = {
    dark: {
      url: 'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}',
      attribution: '&copy; Esri, HERE, Garmin, USGS, NGA, EPA, USDA, NPS',
    },
    satellite: {
      url: 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
      attribution: '&copy; Esri &mdash; Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP',
    },
    street: {
      url: 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
    },
  };

  // Helper to get custom pulsating HTML Marker Icons
  const createGlowingIcon = (severity: SeverityLevel, isSelected: boolean) => {
    let color = '#10B981'; // Green (Safe)
    let pulseClass = 'marker-pulse-safe';
    let ringColor = 'rgba(16, 185, 129, 0.4)';
    let innerShadow = '0 0 12px rgba(16, 185, 129, 0.9)';

    if (severity === 'High Danger') {
      color = '#EF4444'; // Red
      pulseClass = 'marker-pulse-danger';
      ringColor = 'rgba(239, 68, 68, 0.5)';
      innerShadow = '0 0 16px rgba(239, 68, 68, 1)';
    } else if (severity === 'Moderate Warning') {
      color = '#F59E0B'; // Amber
      pulseClass = 'marker-pulse-warning';
      ringColor = 'rgba(245, 158, 11, 0.4)';
      innerShadow = '0 0 14px rgba(245, 158, 11, 0.9)';
    }

    const size = isSelected ? 36 : 28;

    const html = `
      <div style="position: relative; width: ${size}px; height: ${size}px; display: flex; align-items: center; justify-content: center;">
        <div class="${pulseClass}" style="
          position: absolute;
          width: ${size}px;
          height: ${size}px;
          border-radius: 50%;
          background: ${ringColor};
          border: 2px solid ${color};
        "></div>
        <div style="
          width: ${isSelected ? 16 : 12}px;
          height: ${isSelected ? 16 : 12}px;
          border-radius: 50%;
          background-color: ${color};
          box-shadow: ${innerShadow};
          border: 2px solid #ffffff;
          z-index: 2;
        "></div>
      </div>
    `;

    return L.divIcon({
      html,
      className: 'custom-leaflet-marker',
      iconSize: [size, size],
      iconAnchor: [size / 2, size / 2],
      popupAnchor: [0, -size / 2],
    });
  };

  const getCategoryIcon = (category: string) => {
    switch (category) {
      case 'Floods': return <Waves className="w-3.5 h-3.5 text-blue-400" />;
      case 'Forest Fires': return <Flame className="w-3.5 h-3.5 text-orange-400" />;
      case 'Earthquakes': return <Activity className="w-3.5 h-3.5 text-yellow-400" />;
      case 'Cyclones': return <Wind className="w-3.5 h-3.5 text-cyan-400" />;
      case 'Landslides': return <Mountain className="w-3.5 h-3.5 text-amber-400" />;
      default: return <AlertTriangle className="w-3.5 h-3.5 text-red-400" />;
    }
  };

  return (
    <div className="relative w-full h-full bg-[#070a13] overflow-hidden">
      {/* Map Header Overlay Bar */}
      <div className="absolute top-4 left-4 z-[400] flex items-center gap-2 select-none">
        <div className="bg-slate-900/90 border border-slate-800/90 backdrop-blur-md px-3 py-1.5 rounded-lg shadow-xl flex items-center gap-2.5">
          <div className="w-2.5 h-2.5 rounded-full bg-red-500 animate-ping"></div>
          <span className="text-xs font-bold text-slate-200 tracking-wide">
            LIVE GEOSPATIAL RADAR
          </span>
          <span className="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-cyan-300 font-mono">
            {alerts.length} ZONES ACTIVE
          </span>
        </div>

        {/* Reset View Button */}
        <button
          onClick={() => {
            onSelectAlert(null as any);
            setResetCounter((c) => c + 1);
          }}
          title="Reset to India Center"
          className="bg-slate-900/90 border border-slate-800/90 hover:border-slate-700 hover:bg-slate-800 text-slate-300 hover:text-white p-2 rounded-lg backdrop-blur-md shadow-xl transition-all flex items-center gap-1.5 text-xs font-medium"
        >
          <RotateCcw className="w-3.5 h-3.5 text-cyan-400" />
          <span className="hidden sm:inline">Reset View</span>
        </button>
      </div>

      {/* Top Right Map Layer Controls */}
      <div className="absolute top-4 right-4 z-[400] flex items-center gap-2 select-none">
        {/* Heat Radius Toggle */}
        <button
          onClick={() => setShowHeatCircles(!showHeatCircles)}
          className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium backdrop-blur-md shadow-xl transition-all ${
            showHeatCircles
              ? 'bg-cyan-500/20 border-cyan-500/50 text-cyan-300'
              : 'bg-slate-900/90 border-slate-800 text-slate-400 hover:text-slate-200'
          }`}
        >
          {showHeatCircles ? <Eye className="w-3.5 h-3.5" /> : <EyeOff className="w-3.5 h-3.5" />}
          <span className="hidden sm:inline">Impact Radii</span>
        </button>

        {/* Map Style Selector */}
        <div className="bg-slate-900/90 border border-slate-800/90 backdrop-blur-md p-1 rounded-lg shadow-xl flex items-center gap-1">
          <button
            onClick={() => setMapLayer('dark')}
            className={`px-2.5 py-1 rounded text-xs font-medium transition-all ${
              mapLayer === 'dark'
                ? 'bg-slate-800 text-cyan-400 border border-cyan-500/30'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Dark Canvas
          </button>
          <button
            onClick={() => setMapLayer('satellite')}
            className={`px-2.5 py-1 rounded text-xs font-medium transition-all ${
              mapLayer === 'satellite'
                ? 'bg-slate-800 text-cyan-400 border border-cyan-500/30'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Satellite
          </button>
          <button
            onClick={() => setMapLayer('street')}
            className={`px-2.5 py-1 rounded text-xs font-medium transition-all ${
              mapLayer === 'street'
                ? 'bg-slate-800 text-cyan-400 border border-cyan-500/30'
                : 'text-slate-400 hover:text-slate-200'
            }`}
          >
            Terrain
          </button>
        </div>
      </div>

      {/* Bottom Left Legend */}
      <div className="absolute bottom-4 left-4 z-[400] bg-slate-900/90 border border-slate-800/90 backdrop-blur-md px-3.5 py-2.5 rounded-xl shadow-xl select-none hidden sm:block">
        <h4 className="text-[10px] font-bold uppercase tracking-wider text-slate-400 mb-2">
          Severity Color Matrix
        </h4>
        <div className="flex items-center gap-4 text-xs">
          <div className="flex items-center gap-1.5">
            <span className="w-3 h-3 rounded-full bg-red-500 shadow-sm shadow-red-500 animate-pulse"></span>
            <span className="text-slate-300 font-medium">High Danger (#EF4444)</span>
          </div>
          <div className="flex items-center gap-1.5">
            <span className="w-3 h-3 rounded-full bg-amber-500 shadow-sm shadow-amber-500"></span>
            <span className="text-slate-300 font-medium">Moderate Warning (#F59E0B)</span>
          </div>
          <div className="flex items-center gap-1.5">
            <span className="w-3 h-3 rounded-full bg-emerald-500 shadow-sm shadow-emerald-500"></span>
            <span className="text-slate-300 font-medium">Safe (#10B981)</span>
          </div>
        </div>
      </div>

      {/* Leaflet Map */}
      <MapContainer
        key={resetCounter}
        center={defaultCenter}
        zoom={defaultZoom}
        minZoom={4}
        maxZoom={14}
        zoomControl={false}
        className="w-full h-full z-10"
        style={{ background: '#070a13' }}
      >
        <TileLayer
          url={tileLayers[mapLayer].url}
          attribution={tileLayers[mapLayer].attribution}
          maxZoom={18}
        />

        {/* Smooth controller for selected alert fly-to */}
        <MapController
          center={defaultCenter}
          zoom={defaultZoom}
          selectedCoordinates={selectedAlert ? selectedAlert.coordinates : null}
        />

        {/* Live HUD Tracker */}
        <MousePositionHUD />

        {/* Risk Markers and Impact Radii */}
        {alerts.map((alert) => {
          const isSelected = selectedAlert?.id === alert.id;
          const severityColor =
            alert.severity === 'High Danger'
              ? '#ef4444'
              : alert.severity === 'Moderate Warning'
              ? '#f59e0b'
              : '#10b981';

          return (
            <React.Fragment key={alert.id}>
              {/* Optional Heat Impact Circle */}
              {showHeatCircles && (
                <CircleMarker
                  center={alert.coordinates}
                  radius={alert.severity === 'High Danger' ? 45 : alert.severity === 'Moderate Warning' ? 30 : 18}
                  pathOptions={{
                    color: severityColor,
                    fillColor: severityColor,
                    fillOpacity: isSelected ? 0.25 : 0.12,
                    weight: isSelected ? 2 : 1,
                    dashArray: alert.severity === 'High Danger' ? '4 4' : undefined,
                  }}
                />
              )}

              {/* Glowing Interactive Custom Marker */}
              <Marker
                position={alert.coordinates}
                icon={createGlowingIcon(alert.severity, isSelected)}
                eventHandlers={{
                  click: () => {
                    onSelectAlert(alert);
                  },
                }}
              >
                {/* Hover Tooltip */}
                <Tooltip
                  direction="top"
                  offset={[0, -18]}
                  opacity={1}
                  className="custom-leaflet-tooltip"
                >
                  <div className="text-xs font-semibold text-slate-100 bg-slate-900/90 px-2.5 py-1 rounded shadow-lg border border-slate-700">
                    <div className="flex items-center gap-1.5">
                      <span
                        className={`w-2 h-2 rounded-full ${
                          alert.severity === 'High Danger'
                            ? 'bg-red-500 animate-ping'
                            : alert.severity === 'Moderate Warning'
                            ? 'bg-amber-500'
                            : 'bg-emerald-500'
                        }`}
                      ></span>
                      <span className="font-bold">{alert.location}</span>
                    </div>
                    <div className="text-[10px] text-slate-400 mt-0.5">
                      {alert.category} &bull; Risk Score {alert.riskScore}/100
                    </div>
                  </div>
                </Tooltip>

                {/* Click Popup Card */}
                <Popup className="custom-leaflet-popup" maxWidth={320}>
                  <div className="p-1 select-text">
                    {/* Header */}
                    <div className="flex items-start justify-between gap-2 pb-2 border-b border-slate-700/60">
                      <div className="flex items-center gap-1.5">
                        {getCategoryIcon(alert.category)}
                        <span className="text-[11px] font-bold uppercase tracking-wider text-slate-300">
                          {alert.category}
                        </span>
                      </div>
                      <span
                        className={`px-2 py-0.5 rounded-full text-[10px] font-black uppercase tracking-wider ${
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

                    {/* Title & Location */}
                    <div className="my-2.5">
                      <h3 className="text-sm font-bold text-white leading-tight">
                        {alert.title}
                      </h3>
                      <p className="text-xs text-cyan-300 font-medium mt-1 flex items-center gap-1">
                        <MapPin className="w-3 h-3 text-cyan-400 shrink-0" />
                        {alert.location}
                      </p>
                    </div>

                    {/* Quick Stats Grid */}
                    <div className="grid grid-cols-2 gap-1.5 bg-slate-800/70 p-2 rounded-lg border border-slate-700/50 text-[11px] my-2">
                      <div>
                        <span className="text-slate-400 block text-[10px]">Impacted Pop:</span>
                        <span className="font-semibold text-slate-100">{alert.affectedPopulation}</span>
                      </div>
                      <div>
                        <span className="text-slate-400 block text-[10px]">Risk Score:</span>
                        <span className="font-bold text-red-400">{alert.riskScore} / 100</span>
                      </div>
                      <div className="col-span-2 pt-1 border-t border-slate-700/40">
                        <span className="text-slate-400 block text-[10px]">NDRF Forces:</span>
                        <span className="font-semibold text-emerald-300">{alert.ndrfDeployed}</span>
                      </div>
                    </div>

                    {/* Description */}
                    <p className="text-xs text-slate-300 line-clamp-2 my-2">
                      {alert.description}
                    </p>

                    {/* Action Button */}
                    <button
                      onClick={() => onOpenDetails(alert)}
                      className="w-full mt-2 py-1.5 px-3 rounded-lg bg-gradient-to-r from-red-600 to-orange-600 hover:from-red-500 hover:to-orange-500 text-white text-xs font-bold shadow-md shadow-red-950/50 flex items-center justify-center gap-1.5 transition-all"
                    >
                      <Maximize2 className="w-3.5 h-3.5" />
                      <span>View Situational Report (SitRep)</span>
                    </button>
                  </div>
                </Popup>
              </Marker>
            </React.Fragment>
          );
        })}
      </MapContainer>
    </div>
  );
};
