import React from 'react';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import { RootStackParamList } from '../types/navigation';
import LibraryScreen from '../screens/LibraryScreen';
import ProgramDetailScreen from '../screens/ProgramDetailScreen';
import FavoritesScreen from '../screens/FavoritesScreen';
import MarketplaceScreen from '../screens/MarketplaceScreen';
import MarketDetailScreen from '../screens/MarketDetailScreen';
import BleConnectScreen from '../screens/BleConnectScreen';
import DeviceSettingsScreen from '../screens/DeviceSettingsScreen';

const Stack = createNativeStackNavigator<RootStackParamList>();

export default function RootNavigator() {
  return (
    <Stack.Navigator
      screenOptions={{
        headerShown: false,
        contentStyle: { backgroundColor: '#0E0D0B' },
        animation: 'slide_from_right',
      }}
    >
      <Stack.Screen name="Library" component={LibraryScreen} />
      <Stack.Screen name="ProgramDetail" component={ProgramDetailScreen} />
      <Stack.Screen name="Favorites" component={FavoritesScreen} />
      <Stack.Screen name="Marketplace" component={MarketplaceScreen} />
      <Stack.Screen name="MarketDetail" component={MarketDetailScreen} />
      <Stack.Screen name="BleConnect" component={BleConnectScreen} />
      <Stack.Screen name="DeviceSettings" component={DeviceSettingsScreen} />
    </Stack.Navigator>
  );
}
