import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import { ACTIVE_MODULE } from './lib/activeModule'

// index.html carries a static title, so set it here instead — the browser tab
// is a glance target too, and with two modules in play it should say which one
// this build talks to.
document.title = `${ACTIVE_MODULE.name} Web Configurator`

const app = mount(App, {
  target: document.getElementById('app')!,
})

export default app
