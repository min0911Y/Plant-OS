#ifndef PC_ACPI_AML_H
#define PC_ACPI_AML_H

/* This is a bounded AML declaration reader, not an AML interpreter. */
typedef struct {
  const uint8_t *next;
  const uint8_t *end;
} AcpiAml;

static bool aml_take(AcpiAml *aml, size_t count) {
  if (aml->next > aml->end || count > (size_t)(aml->end - aml->next))
    return false;
  aml->next += count;
  return true;
}

static bool aml_package(AcpiAml *aml, AcpiAml *body) {
  if (aml->next == aml->end)
    return false;
  const uint8_t *start = aml->next;
  unsigned following = *start >> 6;
  if ((size_t)(aml->end - start) < following + 1 ||
      (following != 0 && (*start & 0x30) != 0))
    return false;
  uint32_t length = *start & (following == 0 ? 0x3f : 0x0f);
  for (unsigned i = 0; i < following; i++)
    length |= (uint32_t)start[i + 1] << (4 + i * 8);
  if (length < following + 1 || length > (size_t)(aml->end - start))
    return false;
  *body = (AcpiAml){start + following + 1, start + length};
  aml->next = body->end;
  return true;
}

static bool aml_integer(AcpiAml *aml, uint64_t *value) {
  if (aml->next == aml->end)
    return false;
  uint8_t opcode = *aml->next++;
  if (opcode == 0 || opcode == 1 || opcode == 0xff) {
    *value = opcode == 0xff ? ~(uint64_t)0 : opcode;
    return true;
  }
  unsigned bytes;
  switch (opcode) {
    case 0x0a:
      bytes = 1;
      break;
    case 0x0b:
      bytes = 2;
      break;
    case 0x0c:
      bytes = 4;
      break;
    case 0x0e:
      bytes = 8;
      break;
    default:
      return false;
  }
  const uint8_t *data = aml->next;
  if (!aml_take(aml, bytes))
    return false;
  *value = 0;
  for (unsigned i = 0; i < bytes; i++)
    *value |= (uint64_t)data[i] << (i * 8);
  return true;
}

static bool aml_name(AcpiAml *aml, bool *is_s5) {
  *is_s5 = false;
  if (aml->next == aml->end)
    return false;
  if (*aml->next == '\\' && !aml_take(aml, 1))
    return false;
  if (aml->next == aml->end)
    return false;
  unsigned segments = 1;
  if (*aml->next == 0) {
    aml->next++;
    return true;
  }
  if (*aml->next == 0x2e) {
    segments = 2;
    aml->next++;
  } else if (*aml->next == 0x2f) {
    aml->next++;
    if (aml->next == aml->end)
      return false;
    segments = *aml->next++;
    if (segments == 0)
      return false;
  }
  if (segments > (size_t)(aml->end - aml->next) / 4)
    return false;
  const uint8_t *name = aml->next;
  for (unsigned segment = 0; segment < segments; segment++) {
    for (unsigned character = 0; character < 4; character++) {
      uint8_t c = *aml->next++;
      bool first = character == 0;
      if (first ? !(c == '_' || (c >= 'A' && c <= 'Z'))
                : !(c == '_' || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9')))
        return false;
    }
  }
  *is_s5 = segments == 1 && memcmp(name, "_S5_", 4) == 0;
  return true;
}

static bool aml_string(AcpiAml *aml) {
  if (aml->next == aml->end || *aml->next++ != 0x0d)
    return false;
  while (aml->next != aml->end) {
    if (*aml->next++ == 0)
      return true;
  }
  return false;
}

static bool aml_data_object(AcpiAml *aml) {
  if (aml->next == aml->end)
    return false;
  if (*aml->next == 0x0d)
    return aml_string(aml);
  if (*aml->next == 0x11 || *aml->next == 0x12 || *aml->next == 0x13) {
    aml->next++;
    AcpiAml body;
    return aml_package(aml, &body);
  }
  uint64_t value;
  return aml_integer(aml, &value);
}

static bool aml_term_arg(AcpiAml *aml) {
  AcpiAml candidate = *aml;
  if (aml_data_object(&candidate)) {
    *aml = candidate;
    return true;
  }
  candidate = *aml;
  bool ignored;
  if (aml_name(&candidate, &ignored)) {
    *aml = candidate;
    return true;
  }
  return false;
}

static bool aml_s5_package(AcpiAml *aml, uint16_t types[2]) {
  if (aml->next == aml->end || *aml->next++ != 0x12)
    return false;
  AcpiAml body;
  if (!aml_package(aml, &body) || body.next == body.end)
    return false;
  uint8_t elements = *body.next++;
  if (elements < 2)
    return false;
  uint64_t a, b;
  if (!aml_integer(&body, &a) || !aml_integer(&body, &b) || a > 7 || b > 7)
    return false;
  types[0] = (uint16_t)(a << 10);
  types[1] = (uint16_t)(b << 10);
  return true;
}

static bool aml_named_body(AcpiAml *body, unsigned opcode) {
  bool ignored;
  if (!aml_name(body, &ignored))
    return false;
  switch (opcode) {
    case 0x83: /* ProcessorOp: ProcID, PBlockAddr, PBlockLen. */
      return aml_take(body, 6);
    case 0x84: /* PowerResourceOp: SystemLevel, ResourceOrder. */
      return aml_take(body, 3);
    default:
      return true;
  }
}

static bool aml_s5(AcpiAml input, uint16_t types[2]) {
  enum { AML_MAX_SCOPE_DEPTH = 32 };
  AcpiAml scopes[AML_MAX_SCOPE_DEPTH];
  unsigned depth = 1;
  scopes[0] = input;
  while (depth != 0) {
    AcpiAml *aml = &scopes[depth - 1];
    if (aml->next == aml->end) {
      depth--;
      continue;
    }
    uint8_t opcode = *aml->next++;
    if (opcode == 0x06) { /* AliasOp. */
      bool ignored;
      if (!aml_name(aml, &ignored) || !aml_name(aml, &ignored))
        return false;
      continue;
    }
    if (opcode == 0x08) { /* NameOp. */
      bool is_s5;
      if (!aml_name(aml, &is_s5) || aml->next == aml->end)
        return false;
      if (is_s5) {
        if (aml_s5_package(aml, types))
          return true;
        return false;
      }
      if (!aml_term_arg(aml))
        return false;
      continue;
    }
    if (opcode == 0x10 || opcode == 0x14) { /* ScopeOp or MethodOp. */
      AcpiAml body;
      if (!aml_package(aml, &body))
        return false;
      if (opcode == 0x14) /* A method body is executable AML. */
        continue;
      if (!aml_named_body(&body, opcode) || depth == AML_MAX_SCOPE_DEPTH)
        return false;
      scopes[depth++] = body;
      continue;
    }
    if (opcode == 0x5b) { /* ExtendedOpPrefix. */
      if (aml->next == aml->end)
        return false;
      uint8_t extended = *aml->next++;
      if (extended == 0x01) { /* MutexOp. */
        bool ignored;
        if (!aml_name(aml, &ignored) || !aml_take(aml, 1))
          return false;
        continue;
      }
      if (extended == 0x02) { /* EventOp. */
        bool ignored;
        if (!aml_name(aml, &ignored))
          return false;
        continue;
      }
      if (extended == 0x80) { /* OperationRegionOp. */
        bool ignored;
        if (!aml_name(aml, &ignored) || !aml_take(aml, 1) ||
            !aml_term_arg(aml) || !aml_term_arg(aml))
          return false;
        continue;
      }
      AcpiAml body;
      if (extended < 0x81 || extended > 0x87 || !aml_package(aml, &body))
        return false;
      if (extended == 0x82 || extended == 0x83 || extended == 0x84 ||
          extended == 0x85) {
        if (!aml_named_body(&body, extended) || depth == AML_MAX_SCOPE_DEPTH)
          return false;
        scopes[depth++] = body;
      }
      continue;
    }
    if (opcode == 0xa3) /* Noop. */
      continue;
    return false;
  }
  return false;
}

#endif
