export type SeverityLevel = 'High Danger' | 'Moderate Warning' | 'Safe';

export type DisasterCategory = 
  | 'All'
  | 'Floods' 
  | 'Forest Fires' 
  | 'Earthquakes' 
  | 'Cyclones' 
  | 'Landslides' 
  | 'Resources';

export interface DisasterAlert {
  id: string;
  title: string;
  category: Exclude<DisasterCategory, 'All' | 'Resources'>;
  location: string;
  state: string;
  coordinates: [number, number]; // [lat, lng]
  severity: SeverityLevel;
  riskScore: number; // 0 - 100
  affectedPopulation: string;
  ndrfDeployed: string;
  status: 'Critical Alert' | 'Active Warning' | 'Monitored' | 'Contained';
  timestamp: string;
  timeAgo: string;
  description: string;
  details: {
    windSpeed?: string;
    rainfall?: string;
    magnitude?: string;
    depth?: string;
    areaAffected?: string;
    airQualityIndex?: number;
    riverLevel?: string;
    dangerLevel?: string;
    evacuationCount?: string;
    campsOperational?: number;
  };
  recommendations: string[];
}

export interface EmergencyContact {
  id: string;
  name: string;
  agency: string;
  phone: string;
  type: 'National Toll-Free' | 'Armed Forces' | 'State Helpline' | 'Maritime SAR' | 'Medical Emergency';
  status: '24x7 Active' | 'Standby' | 'Operational';
  iconName: string;
}

export interface KeyMetrics {
  totalActiveAlerts: number;
  highDangerCount: number;
  moderateWarningCount: number;
  safeCount: number;
  reliefCampsActive: number;
  ndrfPersonnelDeployed: number;
  nationalRiskIndex: number;
  lastUpdated: string;
}

export interface TrendDataPoint {
  time: string;
  floods: number;
  fires: number;
  earthquakes: number;
  cyclones: number;
  landslides: number;
  severityIndex: number;
}
