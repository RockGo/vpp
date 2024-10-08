/*
 *------------------------------------------------------------------
 * Copyright (c) 2018 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *------------------------------------------------------------------
 */

#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
#include <vlib/pci/pci.h>
#include <vnet/ethernet/ethernet.h>

#include <avf/avf.h>

u8 *
format_avf_device_name (u8 * s, va_list * args)
{
  vlib_main_t *vm = vlib_get_main ();
  u32 i = va_arg (*args, u32);
  avf_device_t *ad = avf_get_device (i);
  vlib_pci_addr_t *addr = vlib_pci_get_addr (vm, ad->pci_dev_handle);

  if (ad->name)
    return format (s, "%s", ad->name);

  s = format (s, "avf-%x/%x/%x/%x",
	      addr->domain, addr->bus, addr->slot, addr->function);
  return s;
}

u8 *
format_avf_device_flags (u8 * s, va_list * args)
{
  avf_device_t *ad = va_arg (*args, avf_device_t *);
  u8 *t = 0;

#define _(a, b, c) if (ad->flags & (1 << a)) \
t = format (t, "%s%s", t ? " ":"", c);
  foreach_avf_device_flags
#undef _
    s = format (s, "%v", t);
  vec_free (t);
  return s;
}

u8 *
format_avf_vf_cap_flags (u8 * s, va_list * args)
{
  u32 flags = va_arg (*args, u32);
  int not_first = 0;

  char *strs[32] = {
#define _(a, b, c) [a] = c,
    foreach_avf_vf_cap_flag
#undef _
  };

  for (int i = 0; i < 32; i++)
    {
      if ((flags & (1 << i)) == 0)
	continue;
      if (not_first)
	s = format (s, " ");
      if (strs[i])
	s = format (s, "%s", strs[i]);
      else
	s = format (s, "unknown(%u)", i);
      not_first = 1;
    }
  return s;
}

static u8 *
format_virtchnl_link_speed (u8 * s, va_list * args)
{
  virtchnl_link_speed_t speed = va_arg (*args, virtchnl_link_speed_t);

  if (speed == 0)
    return format (s, "unknown");
#define _(a, b, c) \
  else if (speed == VIRTCHNL_LINK_SPEED_##b) \
    return format (s, c);
  foreach_virtchnl_link_speed;
#undef _
  return s;
}

u8 *
format_avf_device (u8 * s, va_list * args)
{
  u32 i = va_arg (*args, u32);
  avf_device_t *ad = avf_get_device (i);
  u32 indent = format_get_indent (s);
  u8 *a = 0;
  avf_rxq_t *rxq = vec_elt_at_index (ad->rxqs, 0);
  avf_txq_t *txq = vec_elt_at_index (ad->txqs, 0);
  u32 idx = 0;

  s = format (s, "rx: queues %u, desc %u (min %u max %u)", ad->n_rx_queues,
	      rxq->size, AVF_QUEUE_SZ_MIN, AVF_QUEUE_SZ_MAX);
  s = format (s, "\n%Utx: queues %u, desc %u (min %u max %u)",
	      format_white_space, indent, ad->n_tx_queues, txq->size,
	      AVF_QUEUE_SZ_MIN, AVF_QUEUE_SZ_MAX);
  s = format (s, "\n%Uflags: %U", format_white_space, indent,
	      format_avf_device_flags, ad);
  s = format (s, "\n%Ucapability flags: %U", format_white_space, indent,
	      format_avf_vf_cap_flags, ad->cap_flags);
  s =
    format (s, "\n%U Rx Queue: Total Packets", format_white_space, indent + 4);
  for (idx = 0; idx < ad->n_rx_queues; idx++)
    {
      rxq = vec_elt_at_index (ad->rxqs, idx);
      s = format (s, "\n%U %8u : %llu", format_white_space, indent + 4, idx,
		  rxq->total_packets);
    }
  s = format (s, "\n%U Tx Queue: Total Packets\t Total Drops",
	      format_white_space, indent + 4);
  for (idx = 0; idx < ad->n_tx_queues; idx++)
    {
      txq = vec_elt_at_index (ad->txqs, idx);
      s = format (s, "\n%U %8u : %llu\t %llu", format_white_space, indent + 4,
		  idx, txq->total_packets, txq->no_free_tx_count);
    }

  s = format (s, "\n%Unum-queue-pairs %d max-vectors %u max-mtu %u "
	      "rss-key-size %u rss-lut-size %u", format_white_space, indent,
	      ad->num_queue_pairs, ad->max_vectors, ad->max_mtu,
	      ad->rss_key_size, ad->rss_lut_size);
  s = format (s, "\n%Uspeed %U", format_white_space, indent,
	      format_virtchnl_link_speed, ad->link_speed);
  if (ad->error)
    s = format (s, "\n%Uerror %U", format_white_space, indent,
		format_clib_error, ad->error);

#define _(c) if (ad->eth_stats.c - ad->last_cleared_eth_stats.c) \
  a = format (a, "\n%U%-20U %u", format_white_space, indent + 2, \
	      format_c_identifier, #c,                           \
              ad->eth_stats.c - ad->last_cleared_eth_stats.c);
  foreach_virtchnl_eth_stats;
#undef _
  if (a)
    s = format (s, "\n%Ustats:%v", format_white_space, indent, a);

  vec_free (a);
  return s;
}

u8 *
format_avf_input_trace (u8 * s, va_list * args)
{
  vlib_main_t *vm = va_arg (*args, vlib_main_t *);
  vlib_node_t *node = va_arg (*args, vlib_node_t *);
  avf_input_trace_t *t = va_arg (*args, avf_input_trace_t *);
  vnet_main_t *vnm = vnet_get_main ();
  vnet_hw_interface_t *hi = vnet_get_hw_interface (vnm, t->hw_if_index);
  u32 indent = format_get_indent (s);
  int i = 0;

  s = format (s, "avf: %v (%d) qid %u next-node %U flow-id %u", hi->name,
	      t->hw_if_index, t->qid, format_vlib_next_node_name, vm,
	      node->index, t->next_index, t->flow_id);

  do
    {
      s = format (s, "\n%Udesc %u: status 0x%x error 0x%x ptype 0x%x len %u",
		  format_white_space, indent + 2, i,
		  t->qw1s[i] & pow2_mask (19),
		  (t->qw1s[i] >> AVF_RXD_ERROR_SHIFT) & pow2_mask (8),
		  (t->qw1s[i] >> AVF_RXD_PTYPE_SHIFT) & pow2_mask (8),
		  (t->qw1s[i] >> AVF_RXD_LEN_SHIFT));
    }
  while ((t->qw1s[i++] & AVF_RXD_STATUS_EOP) == 0 &&
	 i < AVF_RX_MAX_DESC_IN_CHAIN);

  return s;
}

u8 *
format_avf_vlan_support (u8 *s, va_list *args)
{
  virtchnl_vlan_support_t v = va_arg (*args, u32);
  int not_first = 0;

  char *strs[32] = {
#define _(a, b, c) [a] = c,
    foreach_virtchnl_vlan_support_bit
#undef _
  };

  if (v == VIRTCHNL_VLAN_UNSUPPORTED)
    return format (s, "unsupported");

  for (int i = 0; i < 32; i++)
    {
      if ((v & (1 << i)) == 0)
	continue;
      if (not_first)
	s = format (s, " ");
      if (strs[i])
	s = format (s, "%s", strs[i]);
      else
	s = format (s, "unknown(%u)", i);
      not_first = 1;
    }
  return s;
}

u8 *
format_avf_vlan_supported_caps (u8 *s, va_list *args)
{
  virtchnl_vlan_supported_caps_t *sc =
    va_arg (*args, virtchnl_vlan_supported_caps_t *);
  u32 indent = format_get_indent (s);

  s = format (s, "outer: %U", format_avf_vlan_support, sc->outer);
  s = format (s, "\n%Uinner: %U", format_white_space, indent,
	      format_avf_vlan_support, sc->inner);
  return s;
}

u8 *
format_avf_vlan_caps (u8 *s, va_list *args)
{
  virtchnl_vlan_caps_t *vc = va_arg (*args, virtchnl_vlan_caps_t *);
  u32 indent = format_get_indent (s);

  s = format (s, "filtering:");
  s = format (s, "\n%Usupport:", format_white_space, indent + 2);
  s =
    format (s, "\n%U%U", format_white_space, indent + 4,
	    format_avf_vlan_supported_caps, &vc->filtering.filtering_support);
  s = format (s, "\n%Uethertype-init: 0x%x", format_white_space, indent + 4,
	      vc->filtering.ethertype_init);
  s = format (s, "\n%Umax-filters: %u", format_white_space, indent + 4,
	      vc->filtering.max_filters);
  s = format (s, "\n%Uoffloads:", format_white_space, indent);
  s = format (s, "\n%Ustripping support:", format_white_space, indent + 2);
  s = format (s, "\n%U%U", format_white_space, indent + 4,
	      format_avf_vlan_supported_caps, &vc->offloads.stripping_support);
  s = format (s, "\n%Uinserion support:", format_white_space, indent + 2);
  s = format (s, "\n%U%U", format_white_space, indent + 4,
	      format_avf_vlan_supported_caps, &vc->offloads.insertion_support);
  s = format (s, "\n%Uethertype-init: 0x%x", format_white_space, indent + 4,
	      vc->offloads.ethertype_init);
  s = format (s, "\n%Uethertype-match: 0x%x", format_white_space, indent + 4,
	      vc->offloads.ethertype_match);
  return s;
}

u8 *
format_avf_eth_stats (u8 *s, va_list *args)
{
  virtchnl_eth_stats_t *es = va_arg (*args, virtchnl_eth_stats_t *);
  u32 indent = format_get_indent (s);
  u8 *v = 0;

#define _(st)                                                                 \
  if (v)                                                                      \
    v = format (v, "\n%U", format_white_space, indent);                       \
  v = format (v, "%-20s = %lu", #st, es->st);
  foreach_virtchnl_eth_stats
#undef _

    s = format (s, "%v", v);
  vec_free (v);
  return s;
}
