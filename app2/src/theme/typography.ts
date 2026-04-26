import { TextStyle } from 'react-native';

export const fonts = {
  sans: 'InterTight',
  sansBold: 'InterTight-Bold',
  sansBlack: 'InterTight-ExtraBold',
  mono: 'JetBrainsMono',
} as const;

export const text: Record<string, TextStyle> = {
  pageTitle: {
    fontFamily: fonts.sansBlack,
    fontSize: 30,
    letterSpacing: -0.7,
  },
  sectionTitle: {
    fontFamily: fonts.sansBold,
    fontSize: 17,
    letterSpacing: -0.3,
  },
  heroTitle: {
    fontFamily: fonts.sansBlack,
    fontSize: 36,
    letterSpacing: -1,
    lineHeight: 38,
  },
  cardTitle: {
    fontFamily: fonts.sansBlack,
    fontSize: 22,
    letterSpacing: -0.5,
  },
  body: {
    fontFamily: fonts.sans,
    fontSize: 14,
  },
  bodySmall: {
    fontFamily: fonts.sans,
    fontSize: 13,
  },
  caption: {
    fontFamily: fonts.sans,
    fontSize: 12,
    fontWeight: '500',
  },
  label: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    textTransform: 'uppercase',
  },
  mono: {
    fontFamily: fonts.mono,
    fontSize: 12,
  },
  monoSmall: {
    fontFamily: fonts.mono,
    fontSize: 11,
  },
  monoTiny: {
    fontFamily: fonts.mono,
    fontSize: 10,
    letterSpacing: 1,
  },
};
