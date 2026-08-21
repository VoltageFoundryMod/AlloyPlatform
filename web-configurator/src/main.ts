import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import { activeModule } from './lib/activeModule'

// index.html carries a static title, so set it here instead — the browser tab
// is a glance target too, and with two modules in play it should say which one
// is on the port.  Follows detection, so a tab left open while modules are
// swapped keeps telling the truth.
activeModule.subscribe((m) => {
  document.title = `${m.name} — Alloy Controller`
})

const app = mount(App, {
  target: document.getElementById('app')!,
})

export default app
