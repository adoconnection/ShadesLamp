import React, { useState } from 'react';
import { View, Text, Pressable, Modal, ScrollView, StyleSheet } from 'react-native';
import { CloseIcon } from './Icon';
import Segmented from './Segmented';
import { fonts } from '../theme/typography';
import { colors } from '../theme/colors';

interface PickerProps {
  options: string[];
  value: number;
  color?: string;
  onChange: (index: number) => void;
  label: string;
}

export default function Picker({ options, value, color = colors.text, onChange, label }: PickerProps) {
  const [open, setOpen] = useState(false);

  if (options.length <= 4) {
    return <Segmented options={options} value={value} color={color} onChange={onChange} />;
  }

  return (
    <>
      <Pressable onPress={() => setOpen(true)} style={styles.button}>
        <View style={styles.buttonLeft}>
          <View style={[styles.dot, { backgroundColor: color }]} />
          <Text style={styles.buttonText}>{options[value]}</Text>
        </View>
        <Text style={styles.counter}>
          {value + 1}/{options.length}
        </Text>
      </Pressable>

      <Modal visible={open} transparent animationType="slide" onRequestClose={() => setOpen(false)}>
        <Pressable style={styles.overlay} onPress={() => setOpen(false)}>
          <Pressable style={styles.sheet} onPress={(e) => e.stopPropagation()}>
            <View style={styles.handle} />
            <View style={styles.sheetHeader}>
              <View>
                <Text style={styles.sheetLabel}>SELECT</Text>
                <Text style={styles.sheetTitle}>{label}</Text>
              </View>
              <Pressable onPress={() => setOpen(false)} style={styles.closeBtn}>
                <CloseIcon size={18} />
              </Pressable>
            </View>
            <ScrollView style={styles.optionsList}>
              {options.map((opt, i) => (
                <Pressable
                  key={i}
                  onPress={() => { onChange(i); setOpen(false); }}
                  style={[
                    styles.option,
                    i === value && { backgroundColor: color + '15' },
                  ]}
                >
                  <View style={styles.optionLeft}>
                    <View
                      style={[
                        styles.radio,
                        i === value
                          ? { backgroundColor: color, borderWidth: 0 }
                          : { borderColor: 'rgba(255,255,255,0.2)', borderWidth: 1.5 },
                      ]}
                    >
                      {i === value && <View style={styles.radioInner} />}
                    </View>
                    <Text style={styles.optionText}>{opt}</Text>
                  </View>
                  <Text style={styles.optionIndex}>{i}</Text>
                </Pressable>
              ))}
            </ScrollView>
          </Pressable>
        </Pressable>
      </Modal>
    </>
  );
}

const styles = StyleSheet.create({
  button: {
    backgroundColor: 'rgba(255,255,255,0.06)',
    borderRadius: 12,
    padding: 12,
    paddingHorizontal: 14,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  buttonLeft: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
  },
  dot: {
    width: 6,
    height: 6,
    borderRadius: 3,
  },
  buttonText: {
    color: colors.text,
    fontSize: 14,
    fontWeight: '500',
  },
  counter: {
    fontFamily: fonts.mono,
    fontSize: 11,
    color: 'rgba(250,250,247,0.45)',
  },
  overlay: {
    flex: 1,
    backgroundColor: 'rgba(0,0,0,0.5)',
    justifyContent: 'flex-end',
  },
  sheet: {
    backgroundColor: '#1A1815',
    borderTopLeftRadius: 28,
    borderTopRightRadius: 28,
    maxHeight: '70%',
    paddingBottom: 30,
  },
  handle: {
    width: 36,
    height: 4,
    borderRadius: 2,
    backgroundColor: 'rgba(255,255,255,0.2)',
    alignSelf: 'center',
    marginTop: 10,
    marginBottom: 4,
  },
  sheetHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 20,
    paddingVertical: 10,
  },
  sheetLabel: {
    fontFamily: fonts.mono,
    fontSize: 11,
    letterSpacing: 1,
    color: 'rgba(250,250,247,0.45)',
    textTransform: 'uppercase',
  },
  sheetTitle: {
    fontSize: 20,
    fontWeight: '700',
    color: colors.text,
    letterSpacing: -0.3,
  },
  closeBtn: {
    width: 32,
    height: 32,
    borderRadius: 16,
    backgroundColor: 'rgba(255,255,255,0.06)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  optionsList: {
    paddingHorizontal: 12,
  },
  option: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: 13,
    paddingHorizontal: 14,
    borderRadius: 12,
  },
  optionLeft: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
  },
  radio: {
    width: 22,
    height: 22,
    borderRadius: 11,
    alignItems: 'center',
    justifyContent: 'center',
  },
  radioInner: {
    width: 10,
    height: 10,
    borderRadius: 5,
    backgroundColor: '#0A0A08',
  },
  optionText: {
    fontSize: 14,
    color: colors.text,
  },
  optionIndex: {
    fontFamily: fonts.mono,
    fontSize: 11,
    color: 'rgba(250,250,247,0.4)',
  },
});
